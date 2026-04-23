#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "python_lttng_ust_tracepoint.h"

/* Helper structure for extracted code information */
typedef struct {
    PyObject *qualname_obj;
    PyObject *filename_obj;
    PyObject *lineno_obj;
    const char *qualname;
    const char *filename;
    int lineno;
} CodeInfo;

/* Helper to extract properties from a PyCodeObject */
static void extract_code_info(PyObject *code, CodeInfo *info) {
    info->qualname_obj = PyObject_GetAttrString(code, "co_qualname");
    info->filename_obj = PyObject_GetAttrString(code, "co_filename");
    info->lineno_obj = PyObject_GetAttrString(code, "co_firstlineno");

    info->qualname = info->qualname_obj ? PyUnicode_AsUTF8(info->qualname_obj) : "<unknown>";
    info->filename = info->filename_obj ? PyUnicode_AsUTF8(info->filename_obj) : "<unknown>";
    info->lineno = info->lineno_obj ? (int)PyLong_AsLong(info->lineno_obj) : 0;
}

/* Helper to clean up extracted property objects */
static void free_code_info(CodeInfo *info) {
    Py_XDECREF(info->qualname_obj);
    Py_XDECREF(info->filename_obj);
    Py_XDECREF(info->lineno_obj);
}

/* sys.monitoring callback: PY_START event */
static PyObject* on_py_start(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 2) Py_RETURN_NONE;

    PyObject *code = args[0];
    CodeInfo info;
    extract_code_info(code, &info);

    unsigned long code_id = (unsigned long)code;
    int python_thread_id = 0; /* Minimal implementation */

    lttng_ust_tracepoint(python_pinsight_lttng_ust, function_begin,
                         info.qualname, info.filename, info.lineno, code_id, python_thread_id);

    free_code_info(&info);
    Py_RETURN_NONE;
}

/* sys.monitoring callback: PY_RETURN event */
static PyObject* on_py_return(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 2) Py_RETURN_NONE;

    PyObject *code = args[0];
    CodeInfo info;
    extract_code_info(code, &info);

    unsigned long code_id = (unsigned long)code;
    int python_thread_id = 0; /* Minimal implementation */

    lttng_ust_tracepoint(python_pinsight_lttng_ust, function_end,
                         info.qualname, info.filename, info.lineno, code_id, python_thread_id);

    free_code_info(&info);
    Py_RETURN_NONE;
}

/* sys.monitoring callback: CALL event (for C extension functions) */
static PyObject* on_call(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 3) Py_RETURN_NONE;

    PyObject *code = args[0];
    PyObject *callable = args[2];

    if (PyCFunction_Check(callable) || Py_IS_TYPE(callable, &PyMethodDescr_Type)) {
        CodeInfo info;
        extract_code_info(code, &info);

        const char *callee_name = "<unknown>";
        PyObject *nameobj = PyObject_GetAttrString(callable, "__qualname__");
        if (!nameobj) {
            PyErr_Clear();
            nameobj = PyObject_GetAttrString(callable, "__name__");
        }
        
        if (nameobj) {
            callee_name = PyUnicode_AsUTF8(nameobj);
        } else {
            PyErr_Clear();
        }

        unsigned long code_id = (unsigned long)code;

        lttng_ust_tracepoint(python_pinsight_lttng_ust, c_call_begin,
                             info.qualname, callee_name, info.filename,
                             info.lineno, code_id);

        free_code_info(&info);
        Py_XDECREF(nameobj);
    }

    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"on_py_start",  (PyCFunction)on_py_start,  METH_FASTCALL, "sys.monitoring PY_START callback"},
    {"on_py_return", (PyCFunction)on_py_return, METH_FASTCALL, "sys.monitoring PY_RETURN callback"},
    {"on_call",      (PyCFunction)on_call,      METH_FASTCALL, "sys.monitoring CALL callback"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_pinsight_python",
    "PInsight Python Trace Extension",
    -1,
    methods
};

PyMODINIT_FUNC PyInit__pinsight_python(void) {
    return PyModule_Create(&module);
}
