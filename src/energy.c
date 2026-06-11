//
// PInsight energy/power measurement — implementation.
//
// This first slice covers the CPU path via the powercap sysfs interface:
//   /sys/class/powercap/intel-rapl/intel-rapl:N/energy_uj   (microjoules)
//   /sys/class/powercap/intel-rapl/intel-rapl:N/max_energy_range_uj
// On current kernels this interface is populated by both Intel RAPL and AMD,
// so it is the most portable starting point. Each intel-rapl:N node is a CPU
// package (socket). GPU paths and [Energy] config selection arrive in later
// steps; for now every discovered, readable socket is read.
//
// NOTE: since ~2020 (CVE-2020-8694, "PLATYPUS") energy_uj is commonly
// root-only. We probe readability at init and degrade gracefully — reads from
// an unreadable counter yield 0 rather than failing or crashing.
//

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "energy.h"

/* The energy provider is defined in exactly this translation unit. */
#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "energy_lttng_ust_tracepoint.h"

#define POWERCAP_RAPL_BASE "/sys/class/powercap/intel-rapl"

/* Discovered CPU package (socket) state, filled at init. */
static char cpu_energy_path[MAX_ENERGY_PACKAGES][320];
static uint64_t cpu_max_range_uj[MAX_ENERGY_PACKAGES]; /* wraparound bound; 0 if unknown */
static int cpu_readable[MAX_ENERGY_PACKAGES];          /* 1 if energy_uj is readable */
static int num_cpu_sockets = 0;

/* Read a sysfs file holding a single unsigned decimal integer.
 * Returns 0 and stores the value on success, -1 on any failure. */
static int read_u64_file(const char *path, uint64_t *out) {
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  char buf[32];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return -1;
  buf[n] = '\0';
  *out = strtoull(buf, NULL, 10);
  return 0;
}

void pinsight_energy_init(void) {
  num_cpu_sockets = 0;

  for (int n = 0; n < MAX_ENERGY_PACKAGES; n++) {
    char epath[320];
    snprintf(epath, sizeof(epath), POWERCAP_RAPL_BASE "/intel-rapl:%d/energy_uj", n);
    if (access(epath, F_OK) != 0)
      break; /* no intel-rapl:N node — enumeration ends here */

    int s = num_cpu_sockets;
    strncpy(cpu_energy_path[s], epath, sizeof(cpu_energy_path[s]) - 1);
    cpu_energy_path[s][sizeof(cpu_energy_path[s]) - 1] = '\0';

    char mpath[320];
    snprintf(mpath, sizeof(mpath),
             POWERCAP_RAPL_BASE "/intel-rapl:%d/max_energy_range_uj", n);
    if (read_u64_file(mpath, &cpu_max_range_uj[s]) != 0)
      cpu_max_range_uj[s] = 0;

    uint64_t probe;
    cpu_readable[s] = (read_u64_file(epath, &probe) == 0) ? 1 : 0;

    num_cpu_sockets++;
  }

  int readable = 0;
  for (int s = 0; s < num_cpu_sockets; s++)
    readable += cpu_readable[s];

  if (num_cpu_sockets == 0) {
    fprintf(stderr, "PInsight ENERGY: no powercap RAPL interface found under %s; "
                    "CPU energy will be reported as 0\n",
            POWERCAP_RAPL_BASE);
  } else if (readable == 0) {
    fprintf(stderr, "PInsight ENERGY: found %d CPU package(s) but energy_uj is not "
                    "readable (it is typically root-only); CPU energy will be 0\n",
            num_cpu_sockets);
  } else {
    fprintf(stderr, "PInsight ENERGY: monitoring %d CPU package(s) (%d readable)\n",
            num_cpu_sockets, readable);
  }
}

void pinsight_energy_fini(void) {
  /* powercap energy mode opens no persistent resources; nothing to release.
   * (PINSIGHT_POWER polling will add persistent-fd cleanup here.) */
}

void pinsight_energy_read(pinsight_energy_t *e) {
  memset(e, 0, sizeof(*e));
  e->num_cpu_sockets = num_cpu_sockets;
  for (int s = 0; s < num_cpu_sockets; s++) {
    uint64_t v = 0;
    if (cpu_readable[s])
      read_u64_file(cpu_energy_path[s], &v);
    e->cpu_energy_uj[s] = v;
  }
  e->num_gpu_devices = 0; /* GPU paths added in later steps */
}

void pinsight_energy_snapshot_enter(void) {
  pinsight_energy_t e;
  pinsight_energy_read(&e);
  lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_enter,
                       e.cpu_energy_uj[0], e.cpu_energy_uj[1], e.cpu_energy_uj[2],
                       e.cpu_energy_uj[3], e.gpu_energy_mj[0], e.gpu_energy_mj[1],
                       e.gpu_energy_mj[2], e.gpu_energy_mj[3], (uint64_t)0);
}

void pinsight_energy_snapshot_exit(void) {
  pinsight_energy_t e;
  pinsight_energy_read(&e);
  lttng_ust_tracepoint(energy_pinsight_lttng_ust, energy_exit,
                       e.cpu_energy_uj[0], e.cpu_energy_uj[1], e.cpu_energy_uj[2],
                       e.cpu_energy_uj[3], e.gpu_energy_mj[0], e.gpu_energy_mj[1],
                       e.gpu_energy_mj[2], e.gpu_energy_mj[3], (uint64_t)0);
}
