/* Read the RAPL registers on recent (>sandybridge) Intel processors	*/
/* The sysfs powercap interface got into the kernel in 			*/
/*	2d281d8196e38dd (3.13)						*/
/*									*/
/* Compile with:   gcc -O2 -Wall -o rapl-read rapl-read.c -lm		*/
/*									*/
/* Vince Weaver -- vincent.weaver @ maine.edu -- 11 September 2015	*/
/*									*/

#ifndef RAPL_H
#define RAPL_H

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/syscall.h>

#define MAX_CPUS 1024
#define MAX_PACKAGES 16
#define NUM_RAPL_DOMAINS 5

static char rapl_domain_names[NUM_RAPL_DOMAINS][30] = {
    "energy-cores",
    "energy-gpu",
    "energy-pkg",
    "energy-ram",
    "energy-psys",
};

static int valid[MAX_PACKAGES][NUM_RAPL_DOMAINS];


// Attempts a read of a single Sysfs RAPL domain name.
// If the top-level package is to be accessed, use -1 for subdomain.
// Returns: 0 for success, -1 for failure.
int rapl_sysfs_read_name(char* dest, int package, int subdomain);

// Attempts a read of a single Sysfs RAPL energy counter.
// If the top-level package is to be accessed, use -1 for subdomain.
// Returns: 0 for success, -1 for failure.
int rapl_sysfs_read_energy(long long *dest, int package, int subdomain);

// Read across all RAPL subdomains, and record which are valid.
int rapl_sysfs_discover_valid(void);

// 0th index is for package validity.
// 1th index onward is for subdomain validity.
// Read across all RAPL subdomains.
// Requires "valid" 2D array to take the form:
//   int valid[MAX_PACKAGES][NUM_RAPL_DOMAINS];
int rapl_sysfs_enumerate_valid(int valid[][NUM_RAPL_DOMAINS]);

// Reads across all RAPL packages on the system.
// Requires "dest" 1D array to take the form:
//   long long energy_values[MAX_PACKAGES];
int rapl_sysfs_read_packages(long long dest[]);

// 0th index is for package energy.
// 1th index onward is for subdomain energy.
// Read across all RAPL subdomains efficiently.
// Requires "dest" 2D array to take the form:
//   long long energy_values[MAX_PACKAGES][NUM_RAPL_DOMAINS];
int rapl_sysfs_read_all(long long dest[][NUM_RAPL_DOMAINS], int valid[][NUM_RAPL_DOMAINS]);

#endif // RAPL_H
