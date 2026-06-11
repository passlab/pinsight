//
// PInsight energy — CPU sysfs backends: powercap (intel-rapl) and amd_energy.
//
// powercap: /sys/class/powercap/intel-rapl/intel-rapl:N/energy_uj  (microjoules)
//           Populated by both Intel RAPL and AMD on current kernels; each
//           intel-rapl:N node is a CPU package (socket).
// amd_energy: /sys/class/hwmon/hwmonN (name == "amd_energy"), per-socket totals
//           identified by an "Esocket*" energy*_label; energy*_input is µJ.
//
// Both counters are commonly root-only since CVE-2020-8694 (PLATYPUS); each
// backend probes readability at init and reports 0 readable sources when locked,
// so the coordinator simply skips it (graceful degradation).
//
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "energy_backend.h"

/* Read a sysfs file holding one unsigned decimal integer. 0 / -1 on success / failure. */
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

/* ===================== powercap (intel-rapl) ===================== */

#define POWERCAP_RAPL_BASE "/sys/class/powercap/intel-rapl"

static char powercap_path[MAX_ENERGY_PACKAGES][320];
static uint64_t powercap_max_range_uj[MAX_ENERGY_PACKAGES]; /* wrap bound; 0 unknown */
static int powercap_readable[MAX_ENERGY_PACKAGES];
static int powercap_n = 0;

static int powercap_init(void) {
  powercap_n = 0;
  int readable = 0;
  for (int n = 0; n < MAX_ENERGY_PACKAGES; n++) {
    char epath[320];
    snprintf(epath, sizeof(epath), POWERCAP_RAPL_BASE "/intel-rapl:%d/energy_uj", n);
    if (access(epath, F_OK) != 0)
      break;
    int s = powercap_n;
    strncpy(powercap_path[s], epath, sizeof(powercap_path[s]) - 1);
    powercap_path[s][sizeof(powercap_path[s]) - 1] = '\0';

    char mpath[320];
    snprintf(mpath, sizeof(mpath),
             POWERCAP_RAPL_BASE "/intel-rapl:%d/max_energy_range_uj", n);
    if (read_u64_file(mpath, &powercap_max_range_uj[s]) != 0)
      powercap_max_range_uj[s] = 0;

    uint64_t probe;
    powercap_readable[s] = (read_u64_file(epath, &probe) == 0) ? 1 : 0;
    readable += powercap_readable[s];
    powercap_n++;
  }
  if (powercap_n > 0 && readable == 0)
    fprintf(stderr, "PInsight ENERGY: powercap found %d package(s) but energy_uj is "
                    "not readable (typically root-only)\n", powercap_n);
  return readable;
}

static void powercap_read(pinsight_energy_t *e) {
  for (int s = 0; s < powercap_n; s++) {
    uint64_t v = 0;
    if (powercap_readable[s])
      read_u64_file(powercap_path[s], &v);
    energy_push_cpu(e, v);
  }
}

static void powercap_fini(void) {}

const pinsight_energy_backend_t pinsight_energy_backend_powercap = {
    .name = "powercap(intel-rapl)",
    .kind = ENERGY_KIND_CPU,
    .init = powercap_init,
    .read = powercap_read,
    .fini = powercap_fini,
};

/* ===================== amd_energy (hwmon) ===================== */

#define HWMON_BASE "/sys/class/hwmon"

static char amd_energy_path[MAX_ENERGY_PACKAGES][320];
static int amd_energy_readable[MAX_ENERGY_PACKAGES];
static int amd_energy_n = 0;

/* Read a small text sysfs file (e.g. a label) into buf. 0 / -1. */
static int read_text_file(const char *path, char *buf, size_t bufsz) {
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  ssize_t n = read(fd, buf, bufsz - 1);
  close(fd);
  if (n <= 0)
    return -1;
  buf[n] = '\0';
  return 0;
}

static int amd_energy_init(void) {
  amd_energy_n = 0;
  int readable = 0;
  for (int h = 0; h < 64 && amd_energy_n < MAX_ENERGY_PACKAGES; h++) {
    char npath[320];
    snprintf(npath, sizeof(npath), HWMON_BASE "/hwmon%d/name", h);
    char name[64];
    if (read_text_file(npath, name, sizeof(name)) != 0)
      continue; /* hwmon%d may not exist; keep scanning a small range */
    if (strncmp(name, "amd_energy", 10) != 0)
      continue;

    /* Enumerate energyM_input; keep only socket totals (label "Esocket*"). */
    for (int m = 1; m <= 256 && amd_energy_n < MAX_ENERGY_PACKAGES; m++) {
      char ipath[320], lpath[320], label[64];
      snprintf(ipath, sizeof(ipath), HWMON_BASE "/hwmon%d/energy%d_input", h, m);
      if (access(ipath, F_OK) != 0)
        continue;
      snprintf(lpath, sizeof(lpath), HWMON_BASE "/hwmon%d/energy%d_label", h, m);
      if (read_text_file(lpath, label, sizeof(label)) != 0)
        continue; /* label unreadable (root-only) — cannot identify sockets */
      if (strncmp(label, "Esocket", 7) != 0)
        continue;

      int s = amd_energy_n;
      strncpy(amd_energy_path[s], ipath, sizeof(amd_energy_path[s]) - 1);
      amd_energy_path[s][sizeof(amd_energy_path[s]) - 1] = '\0';
      uint64_t probe;
      amd_energy_readable[s] = (read_u64_file(ipath, &probe) == 0) ? 1 : 0;
      readable += amd_energy_readable[s];
      amd_energy_n++;
    }
    break; /* one amd_energy hwmon node */
  }
  if (amd_energy_n > 0 && readable == 0)
    fprintf(stderr, "PInsight ENERGY: amd_energy found %d socket(s) but energy*_input "
                    "is not readable (typically root-only)\n", amd_energy_n);
  return readable;
}

static void amd_energy_read(pinsight_energy_t *e) {
  for (int s = 0; s < amd_energy_n; s++) {
    uint64_t v = 0;
    if (amd_energy_readable[s])
      read_u64_file(amd_energy_path[s], &v);
    energy_push_cpu(e, v);
  }
}

static void amd_energy_fini(void) {}

const pinsight_energy_backend_t pinsight_energy_backend_amd_energy = {
    .name = "amd_energy(hwmon)",
    .kind = ENERGY_KIND_CPU,
    .init = amd_energy_init,
    .read = amd_energy_read,
    .fini = amd_energy_fini,
};
