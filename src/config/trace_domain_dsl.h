
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
 *   TRACE_PUNIT(name_string, low, high)
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

#define TRACE_DOMAIN_BEGIN(display_name) \
    TRACE_IMPL_DOMAIN_BEGIN(display_name)

#define TRACE_DOMAIN_END() \
    TRACE_IMPL_DOMAIN_END()

#define TRACE_PUNIT(name, low, high) \
    TRACE_IMPL_PUNIT(name, low, high)

#define TRACE_SUBDOMAIN_BEGIN(name) \
    TRACE_IMPL_SUBDOMAIN_BEGIN(name)

#define TRACE_SUBDOMAIN_END() \
    TRACE_IMPL_SUBDOMAIN_END()

#define TRACE_EVENT(native_id, name, initial_status) \
    TRACE_IMPL_EVENT(native_id, name, initial_status)

#endif /* TRACE_DOMAIN_DSL_H */
