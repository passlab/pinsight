/* trace_domain_loader.h */
#ifndef TRACE_DOMAIN_LOADER_H
#define TRACE_DOMAIN_LOADER_H

#include "domain_info.h"  /* your struct domain_info[], BitSet, etc. */

/* Implemented in trace_domain_loader.c */
void dsl_add_domain(const char *name);
void dsl_add_punit(const char *name, unsigned low, unsigned high);
void dsl_add_subdomain(const char *name);
void dsl_add_event(int native_id, const char *name, int initial_status);
void dsl_print_domain(struct domain_info *d);

#endif /* TRACE_DOMAIN_LOADER_H */
