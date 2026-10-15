/*
 * pysysmon_sampler.c — the Python interpreter-stack probe (S1).
 *
 * First plugin of the generic sampler framework (pinsight_sampler.c).
 * All Python C-API use is confined to this extension (two-library split:
 * libpinsight.so never links libpython).
 *
 * Mechanism (hybrid design §4.1): a PULLED probe — the framework sampler
 * thread calls pysysmon_stack_probe() on its cadence; one
 * PyGILState_Ensure() per tick walks EVERY PyThreadState of the main
 * interpreter with the official strong-ref accessors
 * (PyThreadState_GetFrame → PyFrame_GetBack → PyFrame_GetCode), giving a
 * consistent whole-process snapshot at one timestamp. The GIL is held for
 * the WALK ONLY: code-object pointers are copied to a private buffer and
 * the LTTng events are emitted after PyGILState_Release(). Discipline
 * (survey REV 11, the dd-trace-py #13567 lesson): hold the GIL across the
 * entire walk; official C-API strong refs only; never carry frame pointers
 * across a GIL release.
 *
 * Thread identity (D3): the sampler cannot read another thread's TLS, so
 * sys.monitoring callbacks register (PyThreadState*, python_thread_id,
 * gtid) here at their once-per-thread init; the probe resolves walked
 * tstates against the table. Never-registered threads emit
 * python_thread_id = -1 with the tstate pointer as the join key.
 *
 * Sampling is INDEPENDENT of the trace-mode ladder (REV 10): this file
 * never consults domain modes; the framework's enable predicate
 * (interval_us > 0) is the only switch.
 *
 * Scope: the MAIN interpreter only (PyInterpreterState_Main) —
 * subinterpreters are not walked; acceptable for every target workload.
 */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "pinsight_sampler.h"
#include "trace_domain_Python.h"

/* provider is compiled in pysysmon_callback.c (TRACEPOINT_DEFINE there);
 * here we only reference it. */
#include "pysysmon_lttng_ust_tracepoint.h"

/* ================================================================
 * Thread registration table (D3)
 * ================================================================ */
#define PYSYSMON_MAX_THREADS 256

typedef struct {
  PyThreadState *tstate;
  int python_thread_id;
  int gtid;
} pysysmon_thread_entry_t;

static pysysmon_thread_entry_t thread_table[PYSYSMON_MAX_THREADS];
static int thread_table_count = 0;
static pthread_mutex_t thread_table_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Called from pysysmon_ensure_thread_init (callback context, GIL held).
 * Also the REV 5 vehicle for later open-lexgion enrichment. */
void pysysmon_sampler_register_thread(PyThreadState *ts, int py_tid,
                                      int gtid) {
  pthread_mutex_lock(&thread_table_mtx);
  for (int i = 0; i < thread_table_count; i++) {
    if (thread_table[i].tstate == ts) { /* recycled tstate ptr: update */
      thread_table[i].python_thread_id = py_tid;
      thread_table[i].gtid = gtid;
      pthread_mutex_unlock(&thread_table_mtx);
      return;
    }
  }
  if (thread_table_count < PYSYSMON_MAX_THREADS) {
    thread_table[thread_table_count].tstate = ts;
    thread_table[thread_table_count].python_thread_id = py_tid;
    thread_table[thread_table_count].gtid = gtid;
    thread_table_count++;
  }
  pthread_mutex_unlock(&thread_table_mtx);
}

static int lookup_python_thread_id(PyThreadState *ts) {
  pthread_mutex_lock(&thread_table_mtx);
  for (int i = 0; i < thread_table_count; i++) {
    if (thread_table[i].tstate == ts) {
      int id = thread_table[i].python_thread_id;
      pthread_mutex_unlock(&thread_table_mtx);
      return id;
    }
  }
  pthread_mutex_unlock(&thread_table_mtx);
  return -1;
}

/* ================================================================
 * Code-object dedup set (S1 interim; S2 replaces with the core code table)
 * Open addressing, power-of-two capacity. Only touched by the sampler
 * thread (single probe instance) — no locking needed.
 * ================================================================ */
#define CODE_SET_CAPACITY (1 << 15) /* 32768 slots */
static const void *code_set[CODE_SET_CAPACITY];
static unsigned int code_set_count = 0;
static int code_set_overflow_warned = 0;

/* returns 1 if newly inserted, 0 if already present or table full */
static int code_set_insert(const void *ptr) {
  unsigned long h =
      ((unsigned long)ptr >> 4) * 2654435761UL & (CODE_SET_CAPACITY - 1);
  for (unsigned int probe = 0; probe < CODE_SET_CAPACITY; probe++) {
    unsigned long idx = (h + probe) & (CODE_SET_CAPACITY - 1);
    if (code_set[idx] == ptr)
      return 0;
    if (code_set[idx] == NULL) {
      if (code_set_count >= CODE_SET_CAPACITY - (CODE_SET_CAPACITY >> 3)) {
        if (!code_set_overflow_warned) {
          fprintf(stderr, "PInsight sampler: code-manifest set nearly full "
                          "(%u code objects) — further code objects emit "
                          "duplicate manifests\n", code_set_count);
          code_set_overflow_warned = 1;
        }
        return 1; /* emit anyway (duplicate manifests are latest-wins) */
      }
      code_set[idx] = ptr;
      code_set_count++;
      return 1;
    }
  }
  return 1;
}

/* ================================================================
 * Sample buffers — heap, owned by the probe (single-threaded use)
 * ================================================================ */
#define MAX_SAMPLED_THREADS 128
#define MAX_SUPPORTED_DEPTH 512 /* buffer cap; config max_depth <= this */

typedef struct {
  unsigned long tstate;
  int python_thread_id;
  unsigned int depth;
  int truncated;
} sample_rec_t;

static sample_rec_t sample_recs[MAX_SAMPLED_THREADS];
static unsigned long *sample_ids; /* [MAX_SAMPLED_THREADS][MAX_SUPPORTED_DEPTH] */

/* Probe-side cost split (framework stats can't see inside the tick):
 * GIL-acquisition wait vs actual walk. The wait is BLOCKED time (~zero CPU;
 * bounded by sys.setswitchinterval when a Python thread is running, worse
 * under multi-thread pure-Python convoy); the walk is the real work. */
static unsigned long long gil_wait_ns_total, gil_wait_ns_max;
static unsigned long long walk_ns_total, walk_ns_max;
static unsigned long probe_ticks;
static unsigned long trunc_ticks; /* review F4: ticks where >MAX_SAMPLED_THREADS
                                   * threads had frames — self-honesty stat */

static inline unsigned long long mono_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000000ULL +
         (unsigned long long)ts.tv_nsec;
}

/* ================================================================
 * The probe
 * ================================================================ */
static void pysysmon_stack_probe(void *arg) {
  (void)arg;
  if (!Py_IsInitialized())
    return;

  unsigned int max_depth =
      domain_sampling_config[Python_domain_index].max_depth;
  if (max_depth == 0 || max_depth > MAX_SUPPORTED_DEPTH)
    max_depth = MAX_SUPPORTED_DEPTH;

  int nrec = 0;

  unsigned long long t_req = mono_ns();
  PyGILState_STATE g = PyGILState_Ensure();
  unsigned long long t_have = mono_ns();
  PyThreadState *self_ts = PyGILState_GetThisThreadState();
  PyInterpreterState *interp = PyInterpreterState_Main();

  for (PyThreadState *ts = PyInterpreterState_ThreadHead(interp);
       ts != NULL; ts = PyThreadState_Next(ts)) {
    if (ts == self_ts)
      continue; /* the sampler's own GILState tstate */
    if (nrec >= MAX_SAMPLED_THREADS) {
      trunc_ticks++; /* review F4: count the tick, don't silently drop */
      break;
    }

    PyFrameObject *frame = PyThreadState_GetFrame(ts); /* strong ref | NULL */
    if (!frame)
      continue; /* no Python frame (idle/native-only thread) */

    sample_rec_t *rec = &sample_recs[nrec];
    unsigned long *ids = sample_ids + (size_t)nrec * MAX_SUPPORTED_DEPTH;
    unsigned int depth = 0;
    rec->truncated = 0;

    while (frame) {
      if (depth >= max_depth) {
        rec->truncated = 1;
        Py_DECREF(frame);
        break;
      }
      PyCodeObject *code = PyFrame_GetCode(frame); /* strong ref, != NULL */
      ids[depth++] = (unsigned long)code;

      if (code_set_insert(code)) {
        /* first sight: emit the manifest under the GIL (attr access) */
        PyObject *qn = PyObject_GetAttrString((PyObject *)code, "co_qualname");
        PyObject *fn = PyObject_GetAttrString((PyObject *)code, "co_filename");
        PyObject *ln =
            PyObject_GetAttrString((PyObject *)code, "co_firstlineno");
        const char *qname = qn ? PyUnicode_AsUTF8(qn) : NULL;
        const char *fname = fn ? PyUnicode_AsUTF8(fn) : NULL;
        int lineno = ln ? (int)PyLong_AsLong(ln) : 0;
        lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, code_manifest,
                             (unsigned long)code, qname ? qname : "<unknown>",
                             fname ? fname : "<unknown>", lineno);
        Py_XDECREF(qn);
        Py_XDECREF(fn);
        Py_XDECREF(ln);
        PyErr_Clear();
      }
      Py_DECREF(code);

      PyFrameObject *back = PyFrame_GetBack(frame); /* strong ref | NULL */
      Py_DECREF(frame);
      frame = back;
    }

    if (depth > 0) {
      rec->tstate = (unsigned long)ts;
      rec->python_thread_id = lookup_python_thread_id(ts);
      rec->depth = depth;
      nrec++;
    }
  }
  PyGILState_Release(g);
  unsigned long long t_done = mono_ns();
  probe_ticks++;
  unsigned long long w = t_have - t_req;
  gil_wait_ns_total += w;
  if (w > gil_wait_ns_max) gil_wait_ns_max = w;
  unsigned long long k = t_done - t_have;
  walk_ns_total += k;
  if (k > walk_ns_max) walk_ns_max = k;

  /* emit OUTSIDE the GIL — the walk, not tracepoint I/O, bounds GIL hold */
  for (int i = 0; i < nrec; i++) {
    sample_rec_t *rec = &sample_recs[i];
    lttng_ust_tracepoint(pysysmon_pinsight_lttng_ust, stack_sample,
                         rec->python_thread_id, rec->tstate, rec->depth,
                         rec->truncated,
                         sample_ids + (size_t)i * MAX_SUPPORTED_DEPTH);
  }
}

/* ================================================================
 * Probe lifecycle (called from module methods in pysysmon_callback.c)
 * ================================================================ */
static pinsight_probe_t python_stack_probe = {
    .name = "python_stack",
    .probe = pysysmon_stack_probe,
    .arg = NULL,
    .infra_min_trace_mode = PINSIGHT_TRACE_MODE_NONE, /* independent of event path */
};
static int probe_registered = 0;

int pysysmon_sampler_start(void) {
  if (probe_registered)
    return 0;
  /* review F6: a restart (e.g. fork child, S3) reports its own numbers —
   * reset the probe-side stats; the code dedup set deliberately persists
   * (manifests already emitted; addresses stay valid across fork). */
  gil_wait_ns_total = gil_wait_ns_max = 0;
  walk_ns_total = walk_ns_max = 0;
  probe_ticks = 0;
  trunc_ticks = 0;
  if (!sample_ids) {
    sample_ids = malloc((size_t)MAX_SAMPLED_THREADS * MAX_SUPPORTED_DEPTH *
                        sizeof(unsigned long));
    if (!sample_ids) {
      fprintf(stderr, "PInsight sampler: sample buffer allocation failed\n");
      return -1;
    }
  }
  python_stack_probe.domain_index = Python_domain_index;
  if (pinsight_probe_register(&python_stack_probe) != 0)
    return -1;
  probe_registered = 1;
  return 0;
}

/* Must be called BEFORE interpreter finalization: the framework joins the
 * sampler thread, so no PyGILState_Ensure can be in flight afterwards. */
void pysysmon_sampler_stop(void) {
  if (!probe_registered)
    return;
  pinsight_probe_unregister(&python_stack_probe);
  probe_registered = 0;
  if (probe_ticks > 0) {
    fprintf(stderr,
            "PInsight sampler: python_stack split: gil-wait ns mean/max = "
            "%llu/%llu (blocked, ~0 CPU), walk ns mean/max = %llu/%llu, "
            "code objects interned = %u, truncated ticks = %lu\n",
            gil_wait_ns_total / probe_ticks, gil_wait_ns_max,
            walk_ns_total / probe_ticks, walk_ns_max, code_set_count,
            trunc_ticks);
  }
}
