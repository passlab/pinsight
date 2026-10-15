#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER pysysmon_pinsight_lttng_ust

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./pysysmon_lttng_ust_tracepoint.h"

#if !defined(_PYSYSMON_LTTNG_UST_TRACEPOINT_H_) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _PYSYSMON_LTTNG_UST_TRACEPOINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>

/* Python function begin — fired on PY_START */
LTTNG_UST_TRACEPOINT_EVENT(pysysmon_pinsight_lttng_ust, function_begin,
    LTTNG_UST_TP_ARGS(
        const char *, qualname,
        const char *, filename,
        int, lineno,
        unsigned long, code_id,
        int, python_thread_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_string(qualname, qualname)
        lttng_ust_field_string(filename, filename)
        lttng_ust_field_integer(int, lineno, lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
        lttng_ust_field_integer(int, python_thread_id, python_thread_id)
    )
)

/* Python function end — fired on PY_RETURN */
LTTNG_UST_TRACEPOINT_EVENT(pysysmon_pinsight_lttng_ust, function_end,
    LTTNG_UST_TP_ARGS(
        const char *, qualname,
        const char *, filename,
        int, lineno,
        unsigned long, code_id,
        int, python_thread_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_string(qualname, qualname)
        lttng_ust_field_string(filename, filename)
        lttng_ust_field_integer(int, lineno, lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
        lttng_ust_field_integer(int, python_thread_id, python_thread_id)
    )
)

/* C extension function call — fired on CALL event when callable is C function */
LTTNG_UST_TRACEPOINT_EVENT(pysysmon_pinsight_lttng_ust, c_call_begin,
    LTTNG_UST_TP_ARGS(
        const char *, caller_qualname,
        const char *, callee_name,
        const char *, caller_filename,
        int, caller_lineno,
        unsigned long, code_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_string(caller_qualname, caller_qualname)
        lttng_ust_field_string(callee_name, callee_name)
        lttng_ust_field_string(caller_filename, caller_filename)
        lttng_ust_field_integer(int, caller_lineno, caller_lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
    )
)

/* C extension function return — fired on C_RETURN event */
LTTNG_UST_TRACEPOINT_EVENT(pysysmon_pinsight_lttng_ust, c_call_end,
    LTTNG_UST_TP_ARGS(
        const char *, caller_qualname,
        const char *, callee_name,
        const char *, caller_filename,
        int, caller_lineno,
        unsigned long, code_id
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_string(caller_qualname, caller_qualname)
        lttng_ust_field_string(callee_name, callee_name)
        lttng_ust_field_string(caller_filename, caller_filename)
        lttng_ust_field_integer(int, caller_lineno, caller_lineno)
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
    )
)

/* ---- Sampling instrument (S1, sampler_impl_plan.md) ---------------------
 * Emitted by the Python stack probe running on a framework sampler thread —
 * NOT by sys.monitoring callbacks. Sample records are independent of the
 * trace-mode ladder (hybrid design REV 10). */

/* Once per newly-seen code object: interns code_id → name (S2 replaces the
 * dedup set with the core code table; the event stays). */
LTTNG_UST_TRACEPOINT_EVENT(pysysmon_pinsight_lttng_ust, code_manifest,
    LTTNG_UST_TP_ARGS(
        unsigned long, code_id,
        const char *, qualname,
        const char *, filename,
        int, lineno
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer_hex(unsigned long, code_id, code_id)
        lttng_ust_field_string(qualname, qualname)
        lttng_ust_field_string(filename, filename)
        lttng_ust_field_integer(int, lineno, lineno)
    )
)

/* One per sampled thread per tick: the interpreter stack as code_id[]
 * (leaf-first). python_thread_id = -1 for threads never seen by a callback
 * (join on tstate). truncated = walk hit max_depth. S2 swaps the array for
 * a CCT leaf id (accepted pre-release format churn, D2). */
LTTNG_UST_TRACEPOINT_EVENT(pysysmon_pinsight_lttng_ust, stack_sample,
    LTTNG_UST_TP_ARGS(
        int, python_thread_id,
        unsigned long, tstate,
        unsigned int, depth,
        int, truncated,
        unsigned long *, code_ids
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(int, python_thread_id, python_thread_id)
        lttng_ust_field_integer_hex(unsigned long, tstate, tstate)
        lttng_ust_field_integer(int, truncated, truncated)
        lttng_ust_field_sequence(unsigned long, code_ids, code_ids,
                                 unsigned int, depth)
    )
)

#ifdef __cplusplus
}
#endif

#endif /* _PYSYSMON_LTTNG_UST_TRACEPOINT_H_ */

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>
