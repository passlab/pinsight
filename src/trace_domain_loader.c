/* trace_domain_loader.c */
#include "trace_domain_loader.h"
#include "bitset.h" /* your BitSet implementation */
#include <stdio.h>
#include <string.h>

/* internal state while expanding a domain definition */
static int current_domain = -1;
static int current_subdomain = -1;
static int current_event_id_mode = TRACE_EVENT_ID_INTERNAL;

static void clear_domain(struct domain_info *d) {
  memset(d, 0, sizeof(*d));

  /* Mark event slots invalid */
  for (int i = 0; i < MAX_NUM_DOMAIN_EVENTS; ++i) {
    d->event_table[i].valid = 0;
  }
}

void dsl_add_domain(const char *name, int event_id_mode) {
  current_domain = num_domain++;
  struct domain_info *d = &domain_info_table[current_domain];

  clear_domain(d);
  strncpy(d->name, name, sizeof(d->name) - 1);

  current_event_id_mode = event_id_mode;

  d->num_events = 0;
  d->event_id_upper = 0;

  /* bitsets cover IDs 0..MAX_DOMAIN_EVENTS-1 */
  d->eventInstallStatus = 0;

  current_subdomain = -1;
}

void dsl_add_punit(const char *name, unsigned low, unsigned high,
                   int (*punit_id_func)(), void *arg, int num_arg) {
  struct domain_info *d = &domain_info_table[current_domain];

  int idx = d->num_punits++;
  struct punit *p = &d->punits[idx];

  strncpy(p->name, name, sizeof(p->name) - 1);
  p->low = low;
  p->high = high;
  if (num_arg == 0) {
    p->punit_id_func.func0 = punit_id_func;
  } else {
    p->punit_id_func.func1 = (int (*)(void *))punit_id_func;
  }
  p->arg = arg;
  p->num_arg = num_arg;
}

void dsl_add_subdomain(const char *name) {
  struct domain_info *d = &domain_info_table[current_domain];

  int idx = d->num_subdomains++;
  struct subdomain *sd = &d->subdomains[idx];

  strncpy(sd->name, name, sizeof(sd->name) - 1);
  sd->events = 0;

  current_subdomain = idx;
}

void dsl_add_event(const char *name, int initial_status, int native_id,
                   void *callback) {
  struct domain_info *d = &domain_info_table[current_domain];
  if (current_subdomain < 0)
    return;

  int event_id;

  if (current_event_id_mode == TRACE_EVENT_ID_NATIVE) {
    event_id = native_id; /* sparse possible */
  } else {
    /* dense append order */
    event_id = d->num_events; /* next available dense id */
  }

  if (event_id < 0 || event_id >= MAX_NUM_DOMAIN_EVENTS)
    return;

  /* If sparse/native mode, reject duplicates */
  if (current_event_id_mode == TRACE_EVENT_ID_NATIVE) {
    if (d->event_table[event_id].valid) {
      fprintf(stderr,
              "DSL ERROR: duplicate event id %d in domain '%s'. "
              "Existing event '%s', new event '%s' ignored.\n",
              event_id, d->name, d->event_table[event_id].name, name);
      return; /* ignore the new addition */
    }
  }

  /* Fill entry */
  struct event *ev = &d->event_table[event_id];
  memset(ev, 0, sizeof(*ev));

  ev->native_id = native_id;
  ev->subdomain = current_subdomain;
  ev->callback = callback;
  ev->valid = 1;

  strncpy(ev->name, name, sizeof(ev->name) - 1);

  /* initial status bit indexed by event_id */
  if (initial_status)
    d->eventInstallStatus |= (1UL << event_id);
  else
    d->eventInstallStatus &= ~(1UL << event_id);

  /* subdomain membership bit indexed by event_id */
  d->subdomains[current_subdomain].events |= (1UL << event_id);

  /* num_events is ALWAYS count of declared events */
  d->num_events++;

  /* event_id_upper supports iteration over table/bitsets */
  if (event_id + 1 > d->event_id_upper)
    d->event_id_upper = event_id + 1;
}