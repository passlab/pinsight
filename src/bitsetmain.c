/*
 * bitsetmain.c
 *
 *  Created on: Dec 2, 2025
 *      Author: yyan7
 */


#include "bitset.h"
#include <stdio.h>

int main(void) {
    BitSet bs;

    /* upper bound must be >= largest number you will parse */
    if (bitset_init(&bs, 64) != 0) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }

    const char *s = "0,2-4,11,12,24-32";
    int rc = bitset_parse_range_list(&bs, s);
    if (rc != 0) {
        fprintf(stderr, "parse error (rc=%d)\n", rc);
        bitset_free(&bs);
        return 1;
    }

    bitset_print(&bs);
    printf("Contains 3?  %d\n", bitset_contains(&bs, 3));
    printf("Contains 10? %d\n", bitset_contains(&bs, 10));

    bitset_free(&bs);
    return 0;
}
