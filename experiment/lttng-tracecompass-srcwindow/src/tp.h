#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER my_provider

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./tp.h"

#if !defined(_TP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _TP_H

#include <lttng/tracepoint.h>
#include <stdio.h>

#include "app.h"

TRACEPOINT_EVENT(
    my_provider,
    simple_event,
    TP_ARGS(
        int, my_integer_arg,
        const char *, my_string_arg
    ),
    TP_FIELDS(
        ctf_string(argc, my_string_arg)
        ctf_integer(int, argv, my_integer_arg)
    )
)

TRACEPOINT_ENUM(
    my_provider,
    my_enum,
    TP_ENUM_VALUES(
        ctf_enum_value("ZERO", 0)
        ctf_enum_value("ONE", 1)
        ctf_enum_value("TWO", 2)
        ctf_enum_range("A RANGE", 52, 125)
        ctf_enum_value("ONE THOUSAND", 1000)
    )
)

TRACEPOINT_EVENT(
    my_provider,
    big_event,
    TP_ARGS(
        int, my_integer_arg,
        const char *, my_string_arg,
        FILE *, stream,
        double, flt_arg,
        int *, array_arg
    ),
    TP_FIELDS(
        ctf_integer(int, int_field1, my_integer_arg * 2)
        ctf_integer_hex(long int, stream_pos, ftell(stream))
        ctf_float(double, float_field, flt_arg)
        ctf_string(string_field, my_string_arg)
        ctf_array(int, array_field, array_arg, 7)
        ctf_array_text(char, array_text_field, array_arg, 5)
        ctf_enum(my_provider, my_enum, int,
                 enum_field, array_arg[1])
    )
)

TRACEPOINT_LOGLEVEL(my_provider, big_event, TRACE_WARNING)

TRACEPOINT_EVENT_CLASS(
    my_provider,
    my_tracepoint_class,
    TP_ARGS(
        int, my_integer_arg,
        struct app_struct *, app_struct_arg
    ),
    TP_FIELDS(
        ctf_integer(int, a, my_integer_arg)
        ctf_integer(unsigned long, b, app_struct_arg->b)
        ctf_string(c, app_struct_arg->c)
    )
)

TRACEPOINT_EVENT_INSTANCE(
    my_provider,
    my_tracepoint_class,
    event_instance1,
    TP_ARGS(
        int, my_integer_arg,
        struct app_struct *, app_struct_arg
    )
)

TRACEPOINT_EVENT_INSTANCE(
    my_provider,
    my_tracepoint_class,
    event_instance2,
    TP_ARGS(
        int, my_integer_arg,
        struct app_struct *, app_struct_arg
    )
)

TRACEPOINT_LOGLEVEL(my_provider, event_instance2, TRACE_INFO)

TRACEPOINT_EVENT_INSTANCE(
    my_provider,
    my_tracepoint_class,
    event_instance3,
    TP_ARGS(
        int, my_integer_arg,
        struct app_struct *, app_struct_arg
    )
)

#endif /* _TP_H */

#include <lttng/tracepoint-event.h>