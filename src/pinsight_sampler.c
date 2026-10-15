/*
 * pinsight_sampler.c — generic sampler framework (S1).
 *
 * See pinsight_sampler.h for the model. Implementation notes:
 *
 *  - One scheduling context ("sched") owns one pthread and a set of probes:
 *    sched[0] is the shared thread; a probe whose domain latched
 *    `sampler_thread = own` gets its own sched. Threads are created lazily
 *    on the first kick that finds an enabled probe with no thread, and park
 *    on a CLOCK_MONOTONIC condvar whenever no probe they schedule is
 *    enabled (zero wakeups while parked).
 *
 *  - Multiplexed deadlines: each probe has its own absolute next_deadline
 *    from its domain's interval_us; the thread sleeps until the earliest
 *    deadline among its enabled probes (pthread_cond_timedwait on the
 *    monotonic condvar, so a kick interrupts the sleep immediately).
 *    Probe callbacks run with the sched mutex RELEASED; timestamps are the
 *    actual call time, not the nominal deadline.
 *
 *  - Deadline catch-up: if a tick lands more than one interval late (e.g.
 *    a blocking sibling probe on the shared thread), the missed periods are
 *    counted in missed_ticks and the deadline jumps to now+interval —
 *    the sample stream is self-honest about its gaps.
 *
 *  - Framework threads take reserved utility gtids (5001+, base in
 *    trace_config.h) via init_thread_data() and set the utility_thread
 *    flag — never renumbered, never confusable with app threads
 *    (thread-numbering D3).
 */
#include "pinsight_sampler.h"
#include "pinsight.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Probe-registry capacity. Derived from MAX_NUM_DOMAINS because every
 * planned probe (Python stack, OMPT state, GPU counters, MPI_T, native
 * stack) is one-per-domain — but that is an ASSUMPTION, not a rule: a
 * probe carries its own domain_index and nothing forbids a domain from
 * registering two probes. If that day comes, size this independently
 * (and give multi-probe domains their own rate keys — today both would
 * share the domain's interval_us). */
#define MAX_PROBES MAX_NUM_DOMAINS
#define MAX_SCHEDS (MAX_PROBES + 1) /* sched[0] shared + one per own-probe */
#define SAMPLER_STACK_SIZE (256 * 1024)

typedef struct pinsight_sampler_sched {
  pthread_t thread;
  int created;
  int stop;
  int gtid; /* reserved utility gtid, assigned in sched_spawn_locked BEFORE
             * pthread_create so the new thread never takes registry_mtx —
             * review F1: unregister holds registry_mtx across pthread_join,
             * so a startup lock there could deadlock */
  pthread_mutex_t mtx;
  pthread_cond_t cv; /* CLOCK_MONOTONIC condvar */
  int cv_initialized;
  pinsight_probe_t *probes[MAX_PROBES];
  int num_probes;
} sampler_sched_t;

static sampler_sched_t scheds[MAX_SCHEDS];
static pthread_mutex_t registry_mtx = PTHREAD_MUTEX_INITIALIZER;
static int num_registered = 0;
static int utility_gtid_counter = PINSIGHT_UTILITY_GTID_BASE + 1;
static unsigned short fraction_xsub[3];
static int fraction_seeded = 0;

/* ---------------- time helpers (CLOCK_MONOTONIC) ---------------- */

static inline void ts_now(struct timespec *ts) {
  clock_gettime(CLOCK_MONOTONIC, ts);
}

static inline void ts_add_us(struct timespec *ts, unsigned long us) {
  ts->tv_sec += us / 1000000UL;
  ts->tv_nsec += (long)(us % 1000000UL) * 1000L;
  if (ts->tv_nsec >= 1000000000L) {
    ts->tv_sec += 1;
    ts->tv_nsec -= 1000000000L;
  }
}

static inline long long ts_diff_ns(const struct timespec *a,
                                   const struct timespec *b) {
  /* a - b in ns */
  return (long long)(a->tv_sec - b->tv_sec) * 1000000000LL +
         (a->tv_nsec - b->tv_nsec);
}

/* ---------------- enable predicate ---------------- */

static int probe_enabled(pinsight_probe_t *p) {
  domain_sampling_config_t *sc = &domain_sampling_config[p->domain_index];
  if (sc->interval_us == 0)
    return 0;
  if (p->fraction_excluded)
    return 0;
  if (p->infra_min_trace_mode != PINSIGHT_TRACE_MODE_NONE &&
      domain_trace_config[p->domain_index].mode <
          (volatile domain_trace_mode_t)p->infra_min_trace_mode) {
    if (!p->warned_min_mode) {
      fprintf(stderr,
              "PInsight sampler: probe '%s' needs %s trace_mode >= %s — "
              "parked until then\n",
              p->name, domain_info_table[p->domain_index].name,
              pinsight_trace_mode_str(p->infra_min_trace_mode));
      p->warned_min_mode = 1;
    }
    return 0;
  }
  return 1;
}

/* Warn once if a reload changed a latched key while the probe is live. */
static void check_latched(pinsight_probe_t *p) {
  domain_sampling_config_t *sc = &domain_sampling_config[p->domain_index];
  if (!p->warned_latched && sc->sampler_thread_own != p->latched_own) {
    fprintf(stderr,
            "PInsight sampler: %s sampler_thread changed by reload but is "
            "latched at start ('%s') — keeping the running topology\n",
            domain_info_table[p->domain_index].name,
            p->latched_own ? "own" : "shared");
    p->warned_latched = 1;
  }
}

/* ---------------- the scheduling loop ---------------- */

static void *sampler_thread_fn(void *arg) {
  sampler_sched_t *sd = (sampler_sched_t *)arg;

  /* Reserved utility gtid (5001+, assigned by sched_spawn_locked): never
   * collides with app threads, never renumbered (thread-numbering D3).
   * Deliberately NO registry_mtx here (review F1). */
  init_thread_data(sd->gtid);
  pinsight_thread_data.utility_thread = 1;

  pthread_mutex_lock(&sd->mtx);
  while (!sd->stop) {
    /* 1. earliest deadline among enabled probes */
    struct timespec now;
    ts_now(&now);
    struct timespec earliest;
    int any = 0;
    for (int i = 0; i < sd->num_probes; i++) {
      pinsight_probe_t *p = sd->probes[i];
      check_latched(p);
      if (!probe_enabled(p)) {
        p->deadline_armed = 0; /* re-arm fresh on next enable */
        continue;
      }
      if (!p->deadline_armed) {
        p->next_deadline = now;
        ts_add_us(&p->next_deadline,
                  domain_sampling_config[p->domain_index].interval_us);
        p->deadline_armed = 1;
      }
      if (!any || ts_diff_ns(&p->next_deadline, &earliest) < 0) {
        earliest = p->next_deadline;
        any = 1;
      }
    }

    /* 2. park (no enabled probe) or sleep to the earliest deadline */
    if (!any) {
      pthread_cond_wait(&sd->cv, &sd->mtx); /* zero wakeups while parked */
      continue;
    }
    int rc = pthread_cond_timedwait(&sd->cv, &sd->mtx, &earliest);
    if (rc != ETIMEDOUT)
      continue; /* kicked: config/mode may have changed — recompute */

    /* 3. run every due probe (mutex released around the callback) */
    ts_now(&now);
    for (int i = 0; i < sd->num_probes; i++) {
      pinsight_probe_t *p = sd->probes[i];
      if (!probe_enabled(p) || !p->deadline_armed)
        continue;
      if (ts_diff_ns(&p->next_deadline, &now) > 0)
        continue; /* not due yet */
      /* ONE read of the hot-reloadable interval for this whole tick —
       * review F2: a reload writing 0 between two reads made the
       * catch-up division below a SIGFPE. 0 here = just disabled. */
      unsigned long interval =
          domain_sampling_config[p->domain_index].interval_us;
      if (interval == 0) {
        p->deadline_armed = 0;
        continue;
      }

      p->in_call = 1;
      pthread_mutex_unlock(&sd->mtx);
      struct timespec t0, t1;
      ts_now(&t0);
      p->probe(p->arg);
      ts_now(&t1);
      unsigned long long cost = (unsigned long long)ts_diff_ns(&t1, &t0);
      p->ticks++;
      p->cost_ns_total += cost;
      if (p->cost_ns_min == 0 || cost < p->cost_ns_min)
        p->cost_ns_min = cost;
      if (cost > p->cost_ns_max)
        p->cost_ns_max = cost;
      pthread_mutex_lock(&sd->mtx);
      p->in_call = 0;
      pthread_cond_broadcast(&sd->cv); /* unregister may be waiting */

      /* 4. advance the deadline; count overruns instead of bursting */
      ts_add_us(&p->next_deadline, interval);
      ts_now(&now);
      if (ts_diff_ns(&p->next_deadline, &now) <= 0) {
        long long behind = -ts_diff_ns(&p->next_deadline, &now);
        unsigned long missed =
            (unsigned long)(behind / ((long long)interval * 1000LL)) + 1;
        p->missed_ticks += missed;
        p->next_deadline = now;
        ts_add_us(&p->next_deadline, interval);
      }
    }
  }
  pthread_mutex_unlock(&sd->mtx);
  return NULL;
}

/* ---------------- lifecycle ---------------- */

static void sched_init(sampler_sched_t *sd) {
  if (sd->cv_initialized)
    return;
  pthread_mutex_init(&sd->mtx, NULL);
  pthread_condattr_t ca;
  pthread_condattr_init(&ca);
  pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
  pthread_cond_init(&sd->cv, &ca);
  pthread_condattr_destroy(&ca);
  sd->cv_initialized = 1;
}

static void sched_spawn_locked(sampler_sched_t *sd) {
  /* registry_mtx held */
  if (sd->created)
    return;
  sd->stop = 0;
  sd->gtid = utility_gtid_counter++; /* registry_mtx held (F1) */
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, SAMPLER_STACK_SIZE);
  if (pthread_create(&sd->thread, &attr, sampler_thread_fn, sd) == 0) {
    sd->created = 1;
  } else {
    fprintf(stderr, "PInsight sampler: pthread_create failed — sampling "
                    "disabled for this scheduling context\n");
  }
  pthread_attr_destroy(&attr);
}

/* Any enabled probe on this sched? (for lazy creation) */
static int sched_has_enabled(sampler_sched_t *sd) {
  for (int i = 0; i < sd->num_probes; i++)
    if (probe_enabled(sd->probes[i]))
      return 1;
  return 0;
}

int pinsight_probe_register(pinsight_probe_t *p) {
  pthread_mutex_lock(&registry_mtx);
  if (num_registered >= MAX_PROBES) {
    pthread_mutex_unlock(&registry_mtx);
    fprintf(stderr, "PInsight sampler: probe registry full — '%s' not "
                    "registered\n", p->name);
    return -1;
  }

  /* zero the framework-owned fields */
  p->sched = NULL;
  p->deadline_armed = 0;
  p->in_call = 0;
  p->fraction_excluded = 0;
  p->fraction_drawn = 0;
  p->warned_latched = 0;
  p->warned_min_mode = 0;
  p->ticks = 0;
  p->missed_ticks = 0;
  p->cost_ns_total = 0;
  p->cost_ns_min = 0;
  p->cost_ns_max = 0;

  domain_sampling_config_t *sc = &domain_sampling_config[p->domain_index];

  /* process_fraction: one Bernoulli draw per probe per process (latched).
   * Defensive: an unseeded/invalid 0 must never exclude everything. */
  if (sc->process_fraction <= 0.0f)
    sc->process_fraction = 1.0f;
  if (!p->fraction_drawn) {
    p->fraction_drawn = 1;
    if (sc->process_fraction < 1.0f) {
      if (!fraction_seeded) {
        long seed = (long)getpid() ^ (long)time(NULL);
        fraction_xsub[0] = (unsigned short)(seed & 0xffff);
        fraction_xsub[1] = (unsigned short)((seed >> 16) & 0xffff);
        fraction_xsub[2] = (unsigned short)((seed >> 8) & 0xffff);
        fraction_seeded = 1;
      }
      if (erand48(fraction_xsub) >= (double)sc->process_fraction) {
        p->fraction_excluded = 1;
        fprintf(stderr,
                "PInsight sampler: probe '%s' excluded by process_fraction "
                "%.3f (pid %d)\n",
                p->name, (double)sc->process_fraction, (int)getpid());
      }
    }
  }

  /* latch topology and assign a scheduling context */
  p->latched_own = sc->sampler_thread_own;
  sampler_sched_t *sd = NULL;
  if (p->latched_own) {
    for (int i = 1; i < MAX_SCHEDS; i++) {
      if (scheds[i].num_probes == 0 && !scheds[i].created) {
        sd = &scheds[i];
        break;
      }
    }
    if (!sd) {
      fprintf(stderr, "PInsight sampler: no free own-thread slot for '%s' — "
                      "falling back to the shared thread\n", p->name);
      sd = &scheds[0];
    }
  } else {
    sd = &scheds[0];
  }
  sched_init(sd);
  pthread_mutex_lock(&sd->mtx);
  sd->probes[sd->num_probes++] = p;
  p->sched = sd;
  pthread_mutex_unlock(&sd->mtx);
  num_registered++;
  pthread_mutex_unlock(&registry_mtx);

  pinsight_sampler_kick(); /* lazily spawns if already enabled */
  return 0;
}

void pinsight_probe_unregister(pinsight_probe_t *p) {
  sampler_sched_t *sd = p->sched;
  if (!sd)
    return;

  pthread_mutex_lock(&registry_mtx);
  pthread_mutex_lock(&sd->mtx);
  /* wait out an in-flight callback of this probe */
  while (p->in_call)
    pthread_cond_wait(&sd->cv, &sd->mtx);
  for (int i = 0; i < sd->num_probes; i++) {
    if (sd->probes[i] == p) {
      sd->probes[i] = sd->probes[sd->num_probes - 1];
      sd->num_probes--;
      break;
    }
  }
  int join_it = (sd->num_probes == 0 && sd->created);
  if (join_it)
    sd->stop = 1;
  pthread_cond_broadcast(&sd->cv);
  pthread_mutex_unlock(&sd->mtx);
  if (join_it) {
    pthread_join(sd->thread, NULL);
    sd->created = 0;
  }
  num_registered--;
  pthread_mutex_unlock(&registry_mtx);
  p->sched = NULL;

  /* per-probe cost summary (design Q5; overhead-self-estimation O1) */
  if (p->ticks > 0) {
    fprintf(stderr,
            "PInsight sampler: probe '%s' summary: ticks=%lu missed=%lu "
            "cost ns min/mean/max = %llu/%llu/%llu\n",
            p->name, p->ticks, p->missed_ticks, p->cost_ns_min,
            p->cost_ns_total / p->ticks, p->cost_ns_max);
  }
}

void pinsight_sampler_kick(void) {
  pthread_mutex_lock(&registry_mtx);
  for (int i = 0; i < MAX_SCHEDS; i++) {
    sampler_sched_t *sd = &scheds[i];
    if (sd->num_probes == 0)
      continue;
    if (!sd->created && sched_has_enabled(sd))
      sched_spawn_locked(sd); /* lazy creation */
    if (sd->created) {
      pthread_mutex_lock(&sd->mtx);
      pthread_cond_broadcast(&sd->cv);
      pthread_mutex_unlock(&sd->mtx);
    }
  }
  pthread_mutex_unlock(&registry_mtx);
}
