#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER yield_suspend 

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./yield_suspend_tp.h"

#if !defined(_YIELD_SUSPEND_TP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _YIELD_SUSPEND_TP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lttng/tracepoint.h>

#define TRACEPOINT_EVENT_MACRO(event_name)                          	\
TRACEPOINT_EVENT(							\
    yield_suspend, event_name,						\
    TP_ARGS(								\
        unsigned short, tid,						\
        int, index							\
    ),									\
    TP_FIELDS(								\
        ctf_integer(unsigned short, tid, tid)				\
        ctf_integer(int, index, index)					\
    )									\
)

TRACEPOINT_EVENT_MACRO(before_yield_tp)
TRACEPOINT_EVENT_MACRO(after_yield_tp)
TRACEPOINT_EVENT_MACRO(before_sleep_tp)
TRACEPOINT_EVENT_MACRO(after_sleep_tp)
TRACEPOINT_EVENT_MACRO(before_suspended_tp)
TRACEPOINT_EVENT_MACRO(after_suspended_tp)

#ifdef __cplusplus
}
#endif

#endif /* _YIELD_SUSPEND_TP_H */

#include <lttng/tracepoint-event.h>
