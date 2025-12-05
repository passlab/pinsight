/*
 * bitsetmain.c
 *
 *  Created on: Dec 2, 2025
 *      Author: yyan7
 */


#include "bitset.h"
#include <stdio.h>
#include <stdlib.h>
#include "openmp_domain.h"

void register_all_domains(void)
{
	struct domain_info *omp = register_openmp_domain();

    printf("Registered domain: %s\n", omp->name);

    dsl_print_domain(omp);  // your pretty-printer
    // register_mpi_domain(); // when you add MPI
}

int main(void) {
    BitSet bs;
    bitset_init(&bs, 63);  // inline (<64 bits)

    bitset_parse_ranges(&bs, "0,2-4,11,12,24-32");

    char *r = bitset_to_rangestring(&bs);
    printf("ranges = %s\n", r);
    free(r);

    char *bin = bitset_to_string(&bs);
    printf("binary = %s\n", bin);
    free(bin);

    printf("count = %zu\n", bitset_count(&bs));

    bitset_free(&bs);

    register_all_domains();
    return 0;
}

