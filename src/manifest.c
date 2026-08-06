//
// PInsight manifest — coordinator and sole translation unit of the
// pinsight_manifest_lttng_ust provider (WS1, pinsight-eval/docs/design/ws1_manifest_design.md (private dev repo)).
//
#define _GNU_SOURCE
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "manifest.h"
#include "pinsight_config.h"
#include "trace_config.h"

/* The manifest provider is defined in exactly this translation unit. */
#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "manifest_lttng_ust_tracepoint.h"

#ifdef PINSIGHT_MPI
extern int mpirank; /* pmpi_mpi.c; env best-effort until MPI_Init */
#endif

#define KV_VALUE_CAP 256

/* ---- Immutable facts, cached once by pinsight_manifest_init ---- */
static uint64_t manifest_seq = 0; /* burst ordinal (atomic increment) */
static char run_id[64];           /* provisional until MPI_Init unifies */
static char exe_path[512];
static char exe_build_id[80];     /* hex; empty if not found */
static char version_str[32];

/* Emit one kv, capping the value at KV_VALUE_CAP ("..." marks truncation).
 * NULL/empty values are skipped — emit what is knowable, never fail. */
static void emit_kv(uint64_t seq, const char *key, const char *value) {
  if (!value || !*value)
    return;
  char buf[KV_VALUE_CAP];
  size_t n = strlen(value);
  if (n < sizeof(buf)) {
    lttng_ust_tracepoint(pinsight_manifest_lttng_ust, manifest_kv, seq, key, value);
  } else {
    memcpy(buf, value, sizeof(buf) - 4);
    memcpy(buf + sizeof(buf) - 4, "...", 4);
    lttng_ust_tracepoint(pinsight_manifest_lttng_ust, manifest_kv, seq, key, buf);
  }
}

static void emit_kv_env(uint64_t seq, const char *key, const char *envname) {
  emit_kv(seq, key, getenv(envname));
}

/* First value in a chain of env names (launcher conventions differ). */
static const char *env_first(const char *const names[]) {
  for (int i = 0; names[i]; i++) {
    const char *v = getenv(names[i]);
    if (v && *v)
      return v;
  }
  return NULL;
}

static int manifest_mpirank(void) {
#ifdef PINSIGHT_MPI
  return mpirank; /* env best-effort at startup, authoritative after MPI_Init */
#else
  static const char *const rank_envs[] = {"PMI_RANK", "FLUX_TASK_RANK",
                                          "SLURM_PROCID", "OMPI_COMM_WORLD_RANK",
                                          NULL};
  const char *v = env_first(rank_envs);
  return v ? atoi(v) : -1;
#endif
}

static int manifest_nprocs_hint(void) {
  static const char *const size_envs[] = {"PMI_SIZE", "FLUX_JOB_SIZE",
                                          "SLURM_NTASKS", "OMPI_COMM_WORLD_SIZE",
                                          NULL};
  const char *v = env_first(size_envs);
  return v ? atoi(v) : -1;
}

/* sched_getaffinity -> compact range list ("0-3,96"). */
static void affinity_str(char *out, size_t cap) {
  out[0] = '\0';
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof(set), &set) != 0)
    return;
  size_t off = 0;
  int n = CPU_SETSIZE;
  for (int i = 0; i < n && off + 16 < cap; i++) {
    if (!CPU_ISSET(i, &set))
      continue;
    int j = i;
    while (j + 1 < n && CPU_ISSET(j + 1, &set))
      j++;
    off += (size_t)snprintf(out + off, cap - off, "%s%d", off ? "," : "", i);
    if (j > i && off + 12 < cap)
      off += (size_t)snprintf(out + off, cap - off, "-%d", j);
    i = j;
  }
}

/* GNU build-id of our own executable: scan PT_NOTE segments of /proc/self/exe
 * for NT_GNU_BUILD_ID. ElfW matches the process's own ELF class. */
static void read_build_id(const char *path, char *out, size_t cap) {
  out[0] = '\0';
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return;
  ElfW(Ehdr) eh;
  if (read(fd, &eh, sizeof(eh)) != sizeof(eh) ||
      memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0)
    goto done;
  for (int i = 0; i < eh.e_phnum; i++) {
    ElfW(Phdr) ph;
    if (pread(fd, &ph, sizeof(ph), (off_t)(eh.e_phoff + (size_t)i * eh.e_phentsize)) != sizeof(ph))
      goto done;
    if (ph.p_type != PT_NOTE)
      continue;
    size_t off = 0;
    while (off + sizeof(ElfW(Nhdr)) <= ph.p_filesz) {
      ElfW(Nhdr) nh;
      if (pread(fd, &nh, sizeof(nh), (off_t)(ph.p_offset + off)) != sizeof(nh))
        goto done;
      size_t name_sz = (nh.n_namesz + 3u) & ~3u;
      size_t desc_sz = (nh.n_descsz + 3u) & ~3u;
      if (nh.n_type == NT_GNU_BUILD_ID && nh.n_namesz == 4 && nh.n_descsz > 0 &&
          nh.n_descsz * 2 + 1 <= cap) {
        char name[4];
        unsigned char desc[64];
        if (nh.n_descsz <= sizeof(desc) &&
            pread(fd, name, 4, (off_t)(ph.p_offset + off + sizeof(nh))) == 4 &&
            memcmp(name, "GNU", 4) == 0 &&
            pread(fd, desc, nh.n_descsz,
                  (off_t)(ph.p_offset + off + sizeof(nh) + name_sz)) == (ssize_t)nh.n_descsz) {
          for (unsigned k = 0; k < nh.n_descsz; k++)
            snprintf(out + 2 * k, 3, "%02x", desc[k]);
          goto done;
        }
      }
      off += sizeof(nh) + name_sz + desc_sz;
    }
  }
done:
  close(fd);
}

/* The effective-config dump cache (hash + buffer) is owned by
 * trace_config.c — refreshed automatically at the end of every
 * pinsight_load_trace_config; this file only reads the atomic hash
 * (pinsight_config_hash_get) per burst. Design §2.8. */

void pinsight_manifest_set_run_id(const char *id) {
  if (!id || !*id)
    return;
  snprintf(run_id, sizeof(run_id), "%s", id);
}

const char *pinsight_manifest_get_run_id(void) { return run_id; }

/* Flush the cached effective-config dump to
 * $PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt (design §2.8).
 * CONTROL-THREAD ONLY (plus its start path): keeps all manifest file I/O off
 * app threads — shared-filesystem metadata ops from N ranks at once are the
 * OS-noise class PInsight must not cause. Content-addressed and idempotent:
 * same config -> same name; write-if-absent (first rank per node wins, the
 * rest cost one access()); tmp + rename for atomicity. Env unset -> return
 * immediately: zero file I/O ever, even across reloads. */
void pinsight_manifest_dump_config(void) {
  const char *dir = getenv("PINSIGHT_MANIFEST_DIR");
  if (!dir || !*dir)
    return;
  uint64_t h = pinsight_config_hash_get();
  if (!h)
    return;
  size_t len = 0;
  const char *buf = pinsight_config_dump_get(&len);
  if (!buf || !len)
    return;
  char path[512];
  snprintf(path, sizeof(path), "%s/pinsight_config.%016lx.txt", dir,
           (unsigned long)h);
  if (access(path, F_OK) == 0)
    return; /* content-addressed: this config is already on disk */
  char tmp[560];
  snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return; /* dir missing/unwritable — fail soft, hash stays in-trace */
  size_t w = fwrite(buf, 1, len, f);
  fclose(f);
  if (w == len)
    rename(tmp, path);
  else
    unlink(tmp);
}

void pinsight_manifest_init(void) {
  /* run_id: launcher-provided (tier 1) else 16 random bytes hex (tier 2/3);
   * MPI unifies the random form at MPI_Init (design §2.5). */
  const char *rid = getenv("PINSIGHT_RUN_ID");
  if (rid && *rid) {
    pinsight_manifest_set_run_id(rid);
  } else {
    unsigned char rnd[16];
    if (getrandom(rnd, sizeof(rnd), 0) == (ssize_t)sizeof(rnd)) {
      for (unsigned i = 0; i < sizeof(rnd); i++)
        snprintf(run_id + 2 * i, 3, "%02x", rnd[i]);
    }
  }

  ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (n > 0)
    exe_path[n] = '\0';
  read_build_id(exe_path[0] ? exe_path : "/proc/self/exe", exe_build_id,
                sizeof(exe_build_id));
  snprintf(version_str, sizeof(version_str), "%d.%d", PINSIGHT_VERSION_MAJOR,
           PINSIGHT_VERSION_MINOR);
}

/* Compiled-in domains (initialized-state refinement is future work). */
static const char *domains_str(void) {
  static char buf[64];
  if (!buf[0]) {
    snprintf(buf, sizeof(buf), "%s",
             ""
#ifdef PINSIGHT_OPENMP
             "OpenMP,"
#endif
#ifdef PINSIGHT_MPI
             "MPI,"
#endif
#ifdef PINSIGHT_CUDA
             "CUDA,"
#endif
#ifdef PINSIGHT_HIP
             "HIP,"
#endif
#ifdef PINSIGHT_PYTHON
             "Python,"
#endif
#ifdef PINSIGHT_ENERGY
             "energy,"
#endif
    );
    size_t n = strlen(buf);
    if (n && buf[n - 1] == ',')
      buf[n - 1] = '\0';
  }
  return buf;
}

/* Physical-device offset actually applied to HIP devIds: first index named in
 * ROCR_VISIBLE_DEVICES (else HIP_VISIBLE_DEVICES) — mirrors
 * hip_visible_device_offset() in roctracer_callback.c. */
static const char *gpu_dev_offset_str(char *buf, size_t cap) {
  const char *v = getenv("ROCR_VISIBLE_DEVICES");
  if (!v || !*v)
    v = getenv("HIP_VISIBLE_DEVICES");
  if (!v || !*v)
    return NULL;
  snprintf(buf, cap, "%d", atoi(v));
  return buf;
}

void pinsight_manifest_emit(const char *reason) {
  /* Skip even the fact-gathering when no session subscribes. */
  if (!lttng_ust_tracepoint_enabled(pinsight_manifest_lttng_ust, manifest_process))
    return;

  uint64_t seq = __atomic_fetch_add(&manifest_seq, 1, __ATOMIC_SEQ_CST);
  uint32_t wgen = lexgion_default_trace_config
                      ? lexgion_default_trace_config->end_action.generation
                      : 0;

  lttng_ust_tracepoint(pinsight_manifest_lttng_ust, manifest_process, seq,
                       reason, manifest_mpirank(), exe_path, wgen,
                       manifest_nprocs_hint());

  /* Identity / provenance */
  emit_kv(seq, "run_id", run_id);
  emit_kv(seq, "pinsight.version", version_str);
  emit_kv(seq, "pinsight.domains", domains_str());
  uint64_t chv = pinsight_config_hash_get();
  if (chv) {
    char chash[24];
    snprintf(chash, sizeof(chash), "%016lx", (unsigned long)chv);
    emit_kv(seq, "pinsight.config_hash", chash);
  }

  /* Launcher (recorded as inherited env — job-level facts reach us here) */
  static const char *const job_envs[] = {"FLUX_JOB_ID", "SLURM_JOB_ID", NULL};
  static const char *const rank_envs[] = {"PMI_RANK", "FLUX_TASK_RANK",
                                          "SLURM_PROCID", "OMPI_COMM_WORLD_RANK",
                                          NULL};
  emit_kv(seq, "launcher.job_id", env_first(job_envs));
  emit_kv(seq, "launcher.rank", env_first(rank_envs));

  /* Binding reality (intent vs reality diagnostics — design §1/§3) */
  emit_kv_env(seq, "gpu.rocr_visible_devices", "ROCR_VISIBLE_DEVICES");
  emit_kv_env(seq, "gpu.hip_visible_devices", "HIP_VISIBLE_DEVICES");
  emit_kv_env(seq, "gpu.cuda_visible_devices", "CUDA_VISIBLE_DEVICES");
  char devoff[16];
  emit_kv(seq, "gpu.physical_dev_offset", gpu_dev_offset_str(devoff, sizeof(devoff)));
  char aff[KV_VALUE_CAP];
  affinity_str(aff, sizeof(aff));
  emit_kv(seq, "cpu.affinity", aff);
  emit_kv_env(seq, "omp.num_threads", "OMP_NUM_THREADS");

  /* Application identity (feeds WS12 symbolization) */
  emit_kv(seq, "host.exe", exe_path);
  emit_kv(seq, "host.exe_build_id", exe_build_id);
  char cwd[KV_VALUE_CAP];
  emit_kv(seq, "host.cwd", getcwd(cwd, sizeof(cwd)));
  {
    char cmdline[KV_VALUE_CAP];
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (f) {
      size_t r = fread(cmdline, 1, sizeof(cmdline) - 1, f);
      fclose(f);
      for (size_t i = 0; i + 1 < r; i++)
        if (cmdline[i] == '\0')
          cmdline[i] = ' ';
      if (r > 0) {
        cmdline[r] = '\0';
        emit_kv(seq, "host.cmdline", cmdline);
      }
    }
  }
}
