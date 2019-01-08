/* Read the RAPL registers on recent (>sandybridge) Intel processors	*/
/* The sysfs powercap interface got into the kernel in 			*/
/*	2d281d8196e38dd (3.13)						*/
/*									*/
/* Compile with:   gcc -O2 -Wall -o rapl-read rapl-read.c -lm		*/
/*									*/
/* Vince Weaver -- vincent.weaver @ maine.edu -- 11 September 2015	*/
/*									*/

#include "rapl.h"


static int total_cores = 0, total_packages = 0;
static int package_map[MAX_PACKAGES];
static FILE* pkg_handles[MAX_PACKAGES];
static FILE* rapl_sysfs_open_file(int package, int subdomain, char* filename);


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
      pkg_handles[package] = rapl_sysfs_open_file(package, -1, "energy_uj");
    }
  }

  total_cores = i;
  return 0;
}


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
int rapl_sysfs_read_name(char* dest, int package, int subdomain) {
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

// Attempts a read of a Sysfs RAPL energy counter, given a file handle.
int rapl_sysfs_read_energy(long long* dest, FILE* fff) {
  // Error check.
  if (fff == NULL) {
    return -1;
  }
  // Read RAPL energy counter.
  fscanf(fff, "%lld", dest);
  return 0;
}

// Closes an open sysfs file handle.
// Note: May not need this since most POSIX kernels automatically clean up
//   open file descriptors on process exit.
int rapl_sysfs_close(FILE* fff) {
  fclose(fff);
  return 0;
}

// Read across all RAPL subdomains.
int rapl_sysfs_discover_valid(void) {
  detect_packages();  // Only really need to do this for now.
  return 0;
}

// 0th index is for package validity.
// 1th index onward is for subdomain validity.
// Read across all RAPL subdomains.
// Requires "valid" 2D array to take the form:
//   int valid[MAX_PACKAGES][NUM_RAPL_DOMAINS];
int rapl_sysfs_enumerate_valid(int valid[][NUM_RAPL_DOMAINS]) {
  int i, j;
  FILE *fff;

  // Ensure package detection has been done.
  detect_packages();

  // Enumerate which package/domain IDs are valid.
  for (j = 0; j < total_packages; j++) {
    i = 0;
    // Attempt to open package-level file. If this fails, we are hosed.
    fff = rapl_sysfs_open_file(j, i, "name");
    if (fff == NULL) {
      //fprintf(stderr, "\tCould not open %s\n", tempfile);
      return -1;
    }
    valid[j][i] = 1;
    fclose(fff);

    // Handle subdomains.
    for (i = 1; i < NUM_RAPL_DOMAINS; i++) {
      fff = rapl_sysfs_open_file(j, i, "name");
      // If subdomain can't be accessed, mark it invalid.
      if (fff == NULL) {
        valid[j][i] = 0;
        continue;
      }
      valid[j][i] = 1;
      fclose(fff);
    }
  }
}

// Reads across all RAPL packages on the system.
// Requires "dest" 1D array to take the form:
//   long long energy_values[MAX_PACKAGES];
int rapl_sysfs_read_packages(long long dest[]) {
  int i, status;
  FILE *fff;

  for (i = 0; i < total_packages; i++) {
    // Read package energy.
    status = rapl_sysfs_read_energy(&dest[i], pkg_handles[i]);
  }
}

// 0th index is for package energy.
// 1th index onward is for subdomain energy.
// Read across all RAPL subdomains efficiently.
// Requires "dest" 2D array to take the form:
//   long long energy_values[MAX_PACKAGES][NUM_RAPL_DOMAINS];
//int rapl_sysfs_read_all(long long dest[][NUM_RAPL_DOMAINS], int valid[][NUM_RAPL_DOMAINS]) {
//  int i, j, status;
//  FILE *fff;
//
//  for (j = 0; j < total_packages; j++) {
//    i = 0;
//    // Skip bad entries.
//    if (!valid[j][i]) { continue; }
//    // Read package energy.
//    status = rapl_sysfs_read_energy(&dest[j][i], &pkg_handles[j]);
//    for (i = 1; i < NUM_RAPL_DOMAINS; i++) {
//      // Skip bad entries.
//      if (!valid[j][i]) { continue; }
//      // Read subdomain energy.
//      status = rapl_sysfs_read_energy(&dest[j][i], &pkg_handles[j]);
//    }
//  }
//}
