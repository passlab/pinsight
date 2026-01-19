/* trace_domain_loader.h */
#ifndef TRACE_DOMAIN_LOADER_H
#define TRACE_DOMAIN_LOADER_H

#include "trace_config.h"

/* Event-ID mode */
#define TRACE_EVENT_ID_INTERNAL   0  /* event_id = dense append order */
#define TRACE_EVENT_ID_NATIVE     1  /* event_id = native_id (may be sparse) */

#define PUNIT_ID_FUNC_0_ARG   0
#define PUNIT_ID_FUNC_1_ARG   1

/* Implemented in trace_domain_loader.c */
void dsl_add_domain(const char *name, int event_id_mode);
void dsl_add_punit(const char *name, unsigned low, unsigned high, int (*punit_id_func)(), void * arg, int num_arg);
void dsl_add_subdomain(const char *name);
void dsl_add_event(int native_id, const char *name, int initial_status);
void dsl_print_domain(struct domain_info *d);
void dsl_print_domain_to_file(struct domain_info *d);

#endif /* TRACE_DOMAIN_LOADER_H */
