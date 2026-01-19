
/* trace_domain_dsl.h */
#ifndef TRACE_DOMAIN_DSL_H
#define TRACE_DOMAIN_DSL_H

/*
 * Generic tracing domain DSL.
 *
 * Domain definition headers (e.g., openmp_domain.h, mpi_domain.h) may use:
 *
 *   TRACE_DOMAIN_BEGIN(domain_display_name)
 *   TRACE_DOMAIN_END()
 *
 *   TRACE_PUNIT(name_string, low, high, punit_id_fun, arg)
 *
 *   TRACE_SUBDOMAIN_BEGIN(subdomain_name_string)
 *   TRACE_SUBDOMAIN_END()
 *
 *   TRACE_EVENT(native_id, event_name_string, initial_status)
 *
 * where:
 *   - domain_display_name: const char * ("OpenMP", "MPI", ...)
 *   - name_string, subdomain_name_string, event_name_string: const char *
 *   - native_id: int  (your native event ID)
 *   - initial_status: 0 or 1
 *
 * These expand into TRACE_IMPL_* macros that must be defined by the
 * including translation unit (usually inside a register_* function).
 */

/* Event-ID mode */
#define TRACE_EVENT_ID_INTERNAL   0
#define TRACE_EVENT_ID_NATIVE     1

#define PUNIT_ID_FUNC_0_ARG   0
#define PUNIT_ID_FUNC_1_ARG   1

#define TRACE_DOMAIN_BEGIN(display_name, event_id_mode) \
    TRACE_IMPL_DOMAIN_BEGIN(display_name, event_id_mode)

#define TRACE_DOMAIN_END() \
    TRACE_IMPL_DOMAIN_END()

#define TRACE_PUNIT(name, low, high, punit_id_func) \
    TRACE_IMPL_PUNIT(name, low, high, punit_id_func, NULL, PUNIT_ID_FUNC_0_ARG)

#define TRACE_PUNIT1(name, low, high, punit_id_func, arg) \
    TRACE_IMPL_PUNIT(name, low, high, punit_id_func, arg, PUNIT_ID_FUNC_1_ARG)

#define TRACE_SUBDOMAIN_BEGIN(name) \
    TRACE_IMPL_SUBDOMAIN_BEGIN(name)

#define TRACE_SUBDOMAIN_END() \
    TRACE_IMPL_SUBDOMAIN_END()

#define TRACE_EVENT(native_id, name, initial_status) \
    TRACE_IMPL_EVENT(native_id, name, initial_status)

#endif /* TRACE_DOMAIN_DSL_H */
