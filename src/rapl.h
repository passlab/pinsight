/* Read the RAPL registers on recent (>sandybridge) Intel processors	*/
/*									*/
/* There are currently three ways to do this:				*/
/*	1. Read the MSRs directly with /dev/cpu/??/msr			*/
/*	2. Use the perf_event_open() interface				*/
/*	3. Read the values from the sysfs powercap interface		*/
/*									*/
/* MSR Code originally based on a (never made it upstream) linux-kernel	*/
/*	RAPL driver by Zhang Rui <rui.zhang@intel.com>			*/
/*	https://lkml.org/lkml/2011/5/26/93				*/
/* Additional contributions by:						*/
/*	Romain Dolbeau -- romain @ dolbeau.org				*/
/*									*/
/* For raw MSR access the /dev/cpu/??/msr driver must be enabled and	*/
/*	permissions set to allow read access.				*/
/*	You might need to "modprobe msr" before it will work.		*/
/*									*/
/* perf_event_open() support requires at least Linux 3.14 and to have	*/
/*	/proc/sys/kernel/perf_event_paranoid < 1			*/
/*									*/
/* the sysfs powercap interface got into the kernel in 			*/
/*	2d281d8196e38dd (3.13)						*/
/*									*/
/* Compile with:   gcc -O2 -Wall -o rapl-read rapl-read.c -lm		*/
/*									*/
/* Vince Weaver -- vincent.weaver @ maine.edu -- 11 September 2015	*/
/*									*/

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

static int total_cores = 0, total_packages = 0;
static int package_map[MAX_PACKAGES];

// Warning: Modifies global state.
// Globals modified:
//   total_cores, total_packages, package_map
static int detect_packages(void) {
  char filename[BUFSIZ];
  FILE *fff;
  int package;
  int i;

  // Initialize cores/packages mapping.
  for (i = 0; i < MAX_PACKAGES; i++) {
    package_map[i] = -1;
  }

  // Iterate through CPUs, checking to see which package each CPU belongs to.
  for (i = 0; i < MAX_CPUS; i++) {
    snprintf(filename, BUFSIZ, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
    fff = fopen(filename, "r");
    // File open failed? Skip this item.
    if (fff == NULL) { break; }

    // Read in package ID from the file.
    fscanf(fff, "%d", &package);
    fclose(fff);

    // Track the package ID.
    if (package_map[package] == -1) {
      total_packages++;
      package_map[package] = i;
    }
  }

  total_cores = i;
  return 0;
}

#define NUM_RAPL_DOMAINS 5

char rapl_domain_names[NUM_RAPL_DOMAINS][30] = {
    "energy-cores",
    "energy-gpu",
    "energy-pkg",
    "energy-ram",
    "energy-psys",
};


// Structure which embodies the state of a single RAPL performance counter.
typedef struct RAPLState {
    int package;
    int subdomain;
    long long value;
} RAPLState;


// Abstracts the error-checking file opens for RAPL stuff.
// Success: Returns valid FILE* pointer.
// Failure: Returns NULL.
static FILE* rapl_sysfs_open_file(int package, int subdomain, char* filename) {
  char basename[256];
  char tempfile[256];
  FILE *fff;

  // If valid subdomain ID provided, generate correct filename for that.
  snprintf(basename, 256, "/sys/class/powercap/intel-rapl/intel-rapl:%d", package);
  if (subdomain >= 0) {
    snprintf(tempfile, 256, "%s/intel-rapl:%d:%d/%s", basename, package, subdomain, filename);
  } else {
    snprintf(tempfile, 256, "%s/%s", basename, filename);
  }

  // Try to open file. If it fails, return failure indication.
  fff = fopen(tempfile, "r");
  if (fff == NULL) {
    fprintf(stderr, "\tCould not open %s\n", tempfile);
  }
  // Callers need to check for NULL.
  return fff;
}

// Attempts a read of a single Sysfs RAPL domain name.
// If the top-level package is to be accessed, use -1 for subdomain.
// Returns: 0 for success, -1 for failure.
static int rapl_sysfs_domain_name(char* dest, int package, int subdomain) {
  FILE* fff;
  // Open RAPL Sysfs file with the domain name inside.
  fff = rapl_sysfs_open_file(package, subdomain, "name");
  // Error check.
  if (fff == NULL) {
    return -1;
  }
  // Read name to dest buffer. (Not sure if there's a more secure way to do this.)
  fscanf(fff, "%s", dest);
  fclose(fff);
  return 0;
}

// Attempts a read of a single Sysfs RAPL energy counter.
// If the top-level package is to be accessed, use -1 for subdomain.
// Returns: 0 for success, -1 for failure.
static int rapl_sysfs_read(RAPLState *dest, int package, int subdomain) {
  FILE* fff;
  // Open RAPL Sysfs file with the energy counter inside.
  fff = rapl_sysfs_open_file(package, subdomain, "energy_uj");
  // Error check.
  if (fff == NULL) {
    return -1;
  }
  // Read RAPL energy counter.
  fscanf(fff, "%lld", dest->value);
  fclose(fff);
  return 0;
}


// Reads all available RAPL domains across all packages.
// Performs a sleep for 1 second in the middle of the routine.
// Warning: Uses global state.
// Global state variables used: total_packages.
// TODO: Remove this, and rework into a "read across all packages" function.
static int rapl_sysfs(int core) {

  char event_names[MAX_PACKAGES][NUM_RAPL_DOMAINS][256];
  char filenames[MAX_PACKAGES][NUM_RAPL_DOMAINS][256];
  char basename[MAX_PACKAGES][256];
  char tempfile[256];
  long long before[MAX_PACKAGES][NUM_RAPL_DOMAINS];
  long long after[MAX_PACKAGES][NUM_RAPL_DOMAINS];
  int valid[MAX_PACKAGES][NUM_RAPL_DOMAINS];
  int i, j;
  FILE *fff;

  printf("\nTrying sysfs powercap interface to gather results\n\n");

  /* /sys/class/powercap/intel-rapl/intel-rapl:0/ */
  /* name has name */
  /* energy_uj has energy */
  /* subdirectories intel-rapl:0:0 intel-rapl:0:1 intel-rapl:0:2 */

  for (j = 0; j < total_packages; j++) {
    i = 0;
    sprintf(basename[j], "/sys/class/powercap/intel-rapl/intel-rapl:%d", j);
    sprintf(tempfile, "%s/name", basename[j]);
    fff = fopen(tempfile, "r");
    if (fff == NULL) {
      fprintf(stderr, "\tCould not open %s\n", tempfile);
      return -1;
    }
    fscanf(fff, "%s", event_names[j][i]);
    valid[j][i] = 1;
    fclose(fff);
    sprintf(filenames[j][i], "%s/energy_uj", basename[j]);

    /* Handle subdomains */
    for (i = 1; i < NUM_RAPL_DOMAINS; i++) {
      sprintf(tempfile, "%s/intel-rapl:%d:%d/name", basename[j], j, i - 1);
      fff = fopen(tempfile, "r");
      if (fff == NULL) {
        // fprintf(stderr,"\tCould not open %s\n",tempfile);
        valid[j][i] = 0;
        continue;
      }
      valid[j][i] = 1;
      fscanf(fff, "%s", event_names[j][i]);
      fclose(fff);
      sprintf(filenames[j][i], "%s/intel-rapl:%d:%d/energy_uj", basename[j], j,
              i - 1);
    }
  }

  /* Gather before values */
  for (j = 0; j < total_packages; j++) {
    for (i = 0; i < NUM_RAPL_DOMAINS; i++) {
      if (valid[j][i]) {
        fff = fopen(filenames[j][i], "r");
        if (fff == NULL) {
          fprintf(stderr, "\tError opening %s!\n", filenames[j][i]);
        } else {
          fscanf(fff, "%lld", &before[j][i]);
          fclose(fff);
        }
      }
    }
  }

  printf("\tSleeping 1 second\n\n");
  sleep(1);

  /* Gather after values */
  for (j = 0; j < total_packages; j++) {
    for (i = 0; i < NUM_RAPL_DOMAINS; i++) {
      if (valid[j][i]) {
        fff = fopen(filenames[j][i], "r");
        if (fff == NULL) {
          fprintf(stderr, "\tError opening %s!\n", filenames[j][i]);
        } else {
          fscanf(fff, "%lld", &after[j][i]);
          fclose(fff);
        }
      }
    }
  }

  for (j = 0; j < total_packages; j++) {
    printf("\tPackage %d\n", j);
    for (i = 0; i < NUM_RAPL_DOMAINS; i++) {
      if (valid[j][i]) {
        printf("\t\t%s\t: %lfJ\n", event_names[j][i],
               ((double)after[j][i] - (double)before[j][i]) / 1000000.0);
      }
    }
  }
  printf("\n");

  return 0;
}
