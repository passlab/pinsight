//
// Created by Yonghong Yan on 3/3/20.
//

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER lttng_pinsight_before_after_main

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./before_after_main_tracepoint.h"

#if !defined(PINSIGHT_BEFORE_AFTER_MAIN_TRACEPOINT_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define PINSIGHT_BEFORE_AFTER_MAIN_TRACEPOINT_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
        lttng_pinsight_before_after_main,
        before_main,
        TP_ARGS(
            char *, hostname,
            unsigned int,         pid
        ),
        TP_FIELDS(
            ctf_string(hostname, hostname)
            ctf_integer(unsigned int, pid, pid)
        )
)

TRACEPOINT_EVENT(
        lttng_pinsight_before_after_main,
        after_main,
        TP_ARGS(
            char *, hostname,
            unsigned int,         pid
        ),
        TP_FIELDS(
            ctf_string(hostname, hostname)
            ctf_integer(unsigned int, pid, pid)
        )
)

#endif /* PINSIGHT_BEFORE_AFTER_MAIN_TRACEPOINT_H */

#include <lttng/tracepoint-event.h>