// domain_info.h (sketch)

#include "bitset.h"

#define MAX_DOMAIN_EVENTS 63
#define MAX_DOMAINS       8

struct domain_info {
    char name[16];

    struct subdomain {
        char name[16];
        BitSet events;
    } subdomains[16];
    int num_subdomains;

    struct event {
        int native_id;
        char name[64];
        int subdomain; // index into subdomains[]
        void *callback;
    } event_table[MAX_DOMAIN_EVENTS];
    int num_events;
    BitSet eventInstallStatus;

    struct punit {
        char name[16];
        unsigned int low;
        unsigned int high;
    } punits[4];
    int num_punits;
};

extern struct domain_info domain_info_table[MAX_DOMAINS];
extern int num_domain;
