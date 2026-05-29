/*
 * pysysmon_callback.c
 *
 * PInsight Python tracing via sys.monitoring (PEP 669, Python 3.12+).
 *
 * This C extension provides 4 callbacks registered by pinsight.py:
 *   on_py_start  — PY_START event (function entry)
 *   on_py_return — PY_RETURN event (function exit)
 *   on_c_start   — CALL event (C extension call begin)
 *   on_c_return  — C_RETURN event (C extension call end)
 *
 * Each callback integrates with PInsight's master infrastructure:
 *   - pinsight_check_pause()  for INTROSPECT cooperative pause
 *   - PINSIGHT_DOMAIN_ACTIVE() for 4-mode domain check
 *   - lexgion_begin/end       for lexgion LRU cache + stack
 *   - lexgion_set_top_trace_bit_domain_event() for rate control
 *   - lexgion_post_trace_update() for auto-trigger mode changes
 *
 * Naming convention follows: ompt_callback.c, cupti_callback.c, pmpi_mpi.c
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "pinsight.h"
#include "pinsight_control_thread.h"
#include "trace_config.h"

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "pysysmon_lttng_ust_tracepoint.h"

/* ================================================================
 * Domain globals — defined here, declared extern in trace_domain_Python.h
 * ================================================================ */
int Python_domain_index;
domain_info_t *Python_domain_info;
domain_trace_config_t *Python_trace_config;

/* ================================================================
 * Event IDs — must match dense order in trace_domain_Python.h DSL
 * ================================================================ */
#define PYSYSMON_EVENT_PY_START   0
#define PYSYSMON_EVENT_PY_RETURN  1
#define PYSYSMON_EVENT_C_START    2
#define PYSYSMON_EVENT_C_RETURN   3

/* ================================================================
 * Per-thread Python state
 *
 * pysysmon_thread_id: domain-specific sequential ID (0, 1, 2...)
 *   used by the punit system for config filtering.
 *   Analogous to omp_get_thread_num() / cudaGetDevice().
 *
 * global_thread_num (via init_thread_data): PInsight-internal
 *   thread ID (3000+) for trace record identification.
 *   Starts at 3000 to avoid colliding with OpenMP (0+) and
 *   CUDA (2000+) thread IDs.
 * ================================================================ */
static _Atomic int pysysmon_thread_counter = 0;
static __thread int pysysmon_thread_id = -1;

static _Atomic int pysysmon_global_thread_counter = 3000;
static __thread int pysysmon_tls_inited = 0;

int pysysmon_get_thread_id(void) {
  if (__builtin_expect(pysysmon_thread_id < 0, 0))
    pysysmon_thread_id =
        __atomic_fetch_add(&pysysmon_thread_counter, 1, __ATOMIC_RELAXED);
  return pysysmon_thread_id;
}

static inline void pysysmon_ensure_thread_init(void) {
  if (__builtin_expect(!pysysmon_tls_inited, 0)) {
    int tid = __atomic_fetch_add(&pysysmon_global_thread_counter, 1,
                                 __ATOMIC_RELAXED);
    init_thread_data(tid);
    pysysmon_get_thread_id(); /* also assign domain-specific ID */
    pysysmon_tls_inited = 1;
  }
}

/* ================================================================
 * Helper: extract code info from PyCodeObject
 * ================================================================ */
typedef struct {
  PyObject *qualname_obj;
  PyObject *filename_obj;
  PyObject *lineno_obj;
  const char *qualname;
  const char *filename;
  int lineno;
} CodeInfo;

static void extract_code_info(PyObject *code, CodeInfo *info) {
  info->qualname_obj = PyObject_GetAttrString(code, "co_qualname");
  info->filename_obj = PyObject_GetAttrString(code, "co_filename");
  info->lineno_obj = PyObject_GetAttrString(code, "co_firstlineno");

  info->qualname =
      info->qualname_obj ? PyUnicode_AsUTF8(info->qualname_obj) : "<unknown>";
  info->filename =
      info->filename_obj ? PyUnicode_AsUTF8(info->filename_obj) : "<unknown>";
  info->lineno = info->lineno_obj ? (int)PyLong_AsLong(info->lineno_obj) : 0;
}

static void free_code_info(CodeInfo *info) {
  Py_XDECREF(info->qualname_obj);
  Py_XDECREF(info->filename_obj);
  Py_XDECREF(info->lineno_obj);
}

/* ================================================================
 * Helper: extract C callable name
 * ================================================================ */
static const char *extract_callable_name(PyObject *callable,
                                         PyObject **nameobj_out) {
  const char *name = "<unknown>";
  PyObject *nameobj = PyObject_GetAttrString(callable, "__qualname__");
  if (!nameobj) {
    PyErr_Clear();
    nameobj = PyObject_GetAttrString(callable, "__name__");
  }
  if (nameobj) {
    name = PyUnicode_AsUTF8(nameobj);
  } else {
    PyErr_Clear();
  }
  *nameobj_out = nameobj;
  return name;
}

/* ================================================================
 * Callback: PY_START — Python function entry
 *
 * sys.monitoring signature: callback(code, instruction_offset)
 * ================================================================ */
static PyObject *on_py_start(PyObject *self, PyObject *const *args,
                             Py_ssize_t nargs) {
  if (nargs < 2)
    Py_RETURN_NONE;

  /* 1. Cooperative pause check (INTROSPECT support) */
  pinsight_check_pause();

  /* 2. Fast-path domain mode check (~2ns volatile read) */
  if (!PINSIGHT_DOMAIN_ACTIVE(
          domain_default_trace_config[Python_domain_index].mode))
    Py_RETURN_NONE;

  /* 3. Thread init (once per thread) */
  pysysmon_ensure_thread_init();

  /* 4. Extract code info */
  PyObject *code = args[0];
  const void *codeptr = (const void *)code; /* lexgion identity = PyCodeObject* */

  /* 5. Lexgion begin (LRU lookup + push stack) */
  lexgion_record_t *record =
      lexgion_begin(PYTHON_LEXGION, PYSYSMON_EVENT_PY_START, codeptr);
  lexgion_t *lgp = record->lgp;

  /* 6. Name resolution for named lexgion config matching.
   * Runs once per unique function per config reload cycle (O(1) check).
   * PyObject_GetAttrString returns a new reference; Py_XDECREF on it is safe
   * because the code object retains its own reference to co_qualname/co_filename,
   * keeping the internal UTF-8 buffer valid for the code object's lifetime. */
  if (lgp->name_resolved_gen != trace_config_change_counter) {
    PyObject *qn = PyObject_GetAttrString(code, "co_qualname");
    PyObject *fn = PyObject_GetAttrString(code, "co_filename");
    lgp->name = qn ? PyUnicode_AsUTF8(qn) : NULL;
    if (fn) {
      const char *fp = PyUnicode_AsUTF8(fn);
      const char *base = fp ? strrchr(fp, '/') : NULL;
      lgp->filename_hint = base ? base + 1 : fp;
    }
    Py_XDECREF(qn);
    Py_XDECREF(fn);
    lgp->name_resolved_gen = trace_config_change_counter;
    lgp->trace_config_change_counter = (unsigned int)-1; /* force config re-resolve */
  }

  /* 7. Rate control + config resolution (TRACING mode only) */
  if (PINSIGHT_SHOULD_TRACE(Python_domain_index)) {
    lexgion_set_top_trace_bit_domain_event(lgp, Python_domain_index,
                                           PYSYSMON_EVENT_PY_START);
  }

  /* 8. Emit tracepoint if trace_bit is set */
  if (PINSIGHT_SHOULD_TRACE(Python_domain_index) && lgp->trace_bit) {
    CodeInfo info;
    extract_code_info(code, &info);
    unsigned long code_id = (unsigned long)code;

    lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, function_begin,
                         info.qualname, info.filename, info.lineno, code_id,
                         pysysmon_thread_id);

    free_code_info(&info);
  }

  Py_RETURN_NONE;
}

/* ================================================================
 * Callback: PY_RETURN — Python function exit
 *
 * sys.monitoring signature: callback(code, instruction_offset, retval)
 * ================================================================ */
static PyObject *on_py_return(PyObject *self, PyObject *const *args,
                              Py_ssize_t nargs) {
  if (nargs < 2)
    Py_RETURN_NONE;
  if (!PINSIGHT_DOMAIN_ACTIVE(
          domain_default_trace_config[Python_domain_index].mode))
    Py_RETURN_NONE;

  /* Pop the lexgion stack */
  lexgion_t *lgp = lexgion_end(NULL);
  if (!lgp)
    Py_RETURN_NONE;

  if (PINSIGHT_SHOULD_TRACE(Python_domain_index) && lgp->trace_bit) {
    PyObject *code = args[0];
    CodeInfo info;
    extract_code_info(code, &info);
    unsigned long code_id = (unsigned long)code;

    lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, function_end,
                         info.qualname, info.filename, info.lineno, code_id,
                         pysysmon_thread_id);

    /* Rate-limit update + auto-trigger check */
    lexgion_post_trace_update(lgp);
    free_code_info(&info);
  }

  Py_RETURN_NONE;
}

/* ================================================================
 * Callback: CALL — C extension function call (bridge begin)
 *
 * sys.monitoring signature: callback(code, instruction_offset, callable, arg0)
 *
 * This does NOT create its own lexgion — it piggybacks on the enclosing
 * Python function's trace decision, same pattern as OpenMP work regions
 * piggybacking on the enclosing parallel region.
 * ================================================================ */
static PyObject *on_c_start(PyObject *self, PyObject *const *args,
                         Py_ssize_t nargs) {
  if (nargs < 3)
    Py_RETURN_NONE;
  if (!PINSIGHT_DOMAIN_ACTIVE(
          domain_default_trace_config[Python_domain_index].mode))
    Py_RETURN_NONE;

  PyObject *callable = args[2];

  /* Only trace C function calls */
  if (PyCFunction_Check(callable) ||
      Py_IS_TYPE(callable, &PyMethodDescr_Type)) {
    lexgion_record_t *parent = pinsight_thread_data.current_record;
    if (parent && parent->lgp &&
        PINSIGHT_SHOULD_TRACE(Python_domain_index) && parent->lgp->trace_bit) {

      PyObject *code = args[0];
      CodeInfo info;
      extract_code_info(code, &info);
      unsigned long code_id = (unsigned long)code;

      PyObject *nameobj = NULL;
      const char *callee_name = extract_callable_name(callable, &nameobj);

      lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, c_call_begin,
                           info.qualname, callee_name, info.filename,
                           info.lineno, code_id);

      free_code_info(&info);
      Py_XDECREF(nameobj);
    }
  }

  Py_RETURN_NONE;
}

/* ================================================================
 * Callback: C_RETURN — C extension function return (bridge end)
 *
 * sys.monitoring signature: callback(code, instruction_offset, callable, arg0)
 *
 * Mirrors on_c_start — same piggyback pattern on enclosing Python function.
 * ================================================================ */
static PyObject *on_c_return(PyObject *self, PyObject *const *args,
                             Py_ssize_t nargs) {
  if (nargs < 3)
    Py_RETURN_NONE;
  if (!PINSIGHT_DOMAIN_ACTIVE(
          domain_default_trace_config[Python_domain_index].mode))
    Py_RETURN_NONE;

  PyObject *callable = args[2];

  if (PyCFunction_Check(callable) ||
      Py_IS_TYPE(callable, &PyMethodDescr_Type)) {
    lexgion_record_t *parent = pinsight_thread_data.current_record;
    if (parent && parent->lgp &&
        PINSIGHT_SHOULD_TRACE(Python_domain_index) && parent->lgp->trace_bit) {

      PyObject *code = args[0];
      CodeInfo info;
      extract_code_info(code, &info);
      unsigned long code_id = (unsigned long)code;

      PyObject *nameobj = NULL;
      const char *callee_name = extract_callable_name(callable, &nameobj);

      lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, c_call_end,
                           info.qualname, callee_name, info.filename,
                           info.lineno, code_id);

      free_code_info(&info);
      Py_XDECREF(nameobj);
    }
  }

  Py_RETURN_NONE;
}

/* ================================================================
 * Python module definition
 * ================================================================ */
static PyMethodDef pysysmon_methods[] = {
    {"on_py_start", (PyCFunction)on_py_start, METH_FASTCALL,
     "sys.monitoring PY_START callback"},
    {"on_py_return", (PyCFunction)on_py_return, METH_FASTCALL,
     "sys.monitoring PY_RETURN callback"},
    {"on_c_start", (PyCFunction)on_c_start, METH_FASTCALL,
     "sys.monitoring CALL callback (C extension begin)"},
    {"on_c_return", (PyCFunction)on_c_return, METH_FASTCALL,
     "sys.monitoring C_RETURN callback (C extension end)"},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef pysysmon_module = {
    PyModuleDef_HEAD_INIT,
    "_pinsight_python",
    "PInsight Python Trace Extension (sys.monitoring, PEP 669)",
    -1,
    pysysmon_methods};

PyMODINIT_FUNC PyInit__pinsight_python(void) {
  /* The Python domain is registered by trace_config.c constructor(101)
   * via register_Python_trace_domain(), which runs when libpinsight.so
   * is loaded (before any Python code). The domain index is already
   * valid at this point. */
  return PyModule_Create(&pysysmon_module);
}
