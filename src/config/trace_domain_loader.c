/* trace_domain_loader.c */
#include <string.h>
#include <stdio.h>
#include "trace_domain_loader.h"
#include "bitset.h"      /* your BitSet implementation */

struct domain_info domain_info_table[MAX_DOMAINS];
int num_domain = 0;

/* internal state while expanding a domain definition */
static int current_domain    = -1;
static int current_subdomain = -1;

void dsl_add_domain(const char *name)
{
    current_domain = num_domain++;
    struct domain_info *d = &domain_info_table[current_domain];

    memset(d, 0, sizeof(*d));
    strncpy(d->name, name, sizeof(d->name) - 1);

    bitset_init(&d->eventInstallStatus, MAX_DOMAIN_EVENTS - 1);
}

void dsl_add_punit(const char *name, unsigned low, unsigned high)
{
    struct domain_info *d = &domain_info_table[current_domain];

    int idx = d->num_punits++;
    struct punit *p = &d->punits[idx];

    strncpy(p->name, name, sizeof(p->name) - 1);
    p->low  = low;
    p->high = high;
}

void dsl_add_subdomain(const char *name)
{
    struct domain_info *d = &domain_info_table[current_domain];

    int idx = d->num_subdomains++;
    struct subdomain *sd = &d->subdomains[idx];

    strncpy(sd->name, name, sizeof(sd->name) - 1);
    bitset_init(&sd->events, MAX_DOMAIN_EVENTS - 1);

    current_subdomain = idx;
}

void dsl_add_event(int native_id,
                   const char *name,
                   int initial_status)
{
    struct domain_info *d = &domain_info_table[current_domain];

    int idx = d->num_events++;
    struct event *ev = &d->event_table[idx];

    ev->native_id = native_id;
    ev->subdomain = current_subdomain;
    ev->callback  = NULL;

    strncpy(ev->name, name, sizeof(ev->name) - 1);

    if (initial_status) {
        bitset_set(&d->eventInstallStatus, idx);
    } else {
        bitset_clear(&d->eventInstallStatus, idx);
    }

    bitset_set(&d->subdomains[current_subdomain].events, idx);
}

#if 0
static void print_indent(int level)
{
    while (level--) printf("    ");
}

void dsl_print_domain(struct domain_info *d)
{
    if (!d) return;

    printf("// Domain: %s\n", d->name);

    /* ------------------ PRINT PUNIT RANGES ------------------ */
    if (d->num_punits > 0) {
        printf("// Punit ranges for domain %s\n", d->name);
        for (int i = 0; i < d->num_punits; i++) {
            struct punit *p = &d->punits[i];
            printf("[%s.%s(%u-%u)]\n",
                   d->name,
                   p->name,
                   p->low, p->high);
        }
        printf("\n");
    }

    /* ------------------ PRINT SUBDOMAINS & EVENTS ------------------ */
    printf("//%s subdomains and events\n", d->name);
    for (int s = 0; s < d->num_subdomains; s++) {
        struct subdomain *sd = &d->subdomains[s];

        printf("[%-s(%s)]\n", d->name, sd->name);

        /* Events belonging to this subdomain */
        for (int e = 0; e < d->num_events; e++) {
            struct event *ev = &d->event_table[e];
            if (ev->subdomain != s) continue;

            int enabled = bitset_test(&d->eventInstallStatus, e);

            print_indent(1);
            printf("%s = %s\n",
                   ev->name,
                   enabled ? "on" : "off");
        }
        printf("\n");
    }
}
#endif

/*
 * Pretty-print a domain into a file:
 *      <domain>_trace_config.install
 */
void dsl_print_domain(struct domain_info *d)
{
    if (!d)
        return;

    char filename[256];
    snprintf(filename, sizeof(filename),
             "%s_trace_config.install", d->name);

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "dsl_print_domain: cannot open file %s\n", filename);
        return;
    }

    /* ---- Print PUNIT ranges ---- */
    for (int i = 0; i < d->num_punits; ++i) {
        struct punit *p = &d->punits[i];
        fprintf(fp, "[%s.%s(%u-%u)]\n\n",
                d->name, p->name, p->low, p->high);
    }

    /* ---- Print subdomains and events ---- */
    for (int s = 0; s < d->num_subdomains; ++s) {
        struct subdomain *sub = &d->subdomains[s];

        fprintf(fp, "[%s(%s)]\n",
                d->name, sub->name);

        /* print events in this subdomain */
        for (int e = 0; e < d->num_events; ++e) {
            struct event *ev = &d->event_table[e];
            if (ev->subdomain == s) {
                int enabled = bitset_test(&d->eventInstallStatus, e);
                fprintf(fp, "    %s = %s\n",
                        ev->name,
                        enabled ? "on" : "off");
            }
        }

        fprintf(fp, "\n");
    }

    fclose(fp);
}


