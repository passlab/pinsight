//
// PInsight manifest — public interface (WS1, pinsight-eval/docs/design/ws1_manifest_design.md (private dev repo)).
//
// The manifest makes traces self-describing: a periodic, latest-wins-per-key
// record of what only this process can know at runtime (env as seen, binding,
// config as parsed, rank, binary identity) plus the run_id join key to the
// launcher-side run_manifest.json sidecar. Emission is a BURST: one
// manifest_process + N manifest_kv events sharing `seq`.
//
#ifndef PINSIGHT_MANIFEST_H
#define PINSIGHT_MANIFEST_H

/* Cache immutable facts (exe path, build-id, version, provisional run_id from
 * PINSIGHT_RUN_ID env else getrandom) once. Called from the library
 * constructor after config load, before the first emit. */
void pinsight_manifest_init(void);

/* Emit one burst. reason = "init" | "mpi_init" | "periodic" | "window" |
 * "fini". Never fails; skips unknowable keys; skips even the fact-gathering
 * when no session subscribes to the provider (tracepoint_enabled guard).
 * Thread-safety: callers are the constructor/MPI_Init wrapper (app thread)
 * and the control thread; concurrent bursts would interleave kv events but
 * per-(seq) payloads stay self-consistent — consumers pair by seq. */
void pinsight_manifest_emit(const char *reason);

/* Note: the effective-config dump cache (buffer + atomic hash) lives in
 * trace_config.{h,c} (pinsight_config_dump_refresh / _hash_get / _dump_get),
 * refreshed automatically by every pinsight_load_trace_config; bursts here
 * only read the hash. Design §2.8. */

/* Replace the provisional per-process run_id with the experiment-wide one
 * (rank 0's, PMPI_Bcast in the MPI_Init wrapper — design §2.5). The next
 * burst re-emits it; latest-wins makes the transition invisible. id must be
 * a NUL-terminated string (33 bytes for the 16-byte-hex form). */
void pinsight_manifest_set_run_id(const char *id);

/* Current run_id (launcher-provided or provisional). Used by the MPI_Init
 * wrapper as the bcast source/destination buffer copy. Always NUL-terminated;
 * may be "" if generation failed. */
const char *pinsight_manifest_get_run_id(void);

/* Flush the cached effective-config dump to
 * $PINSIGHT_MANIFEST_DIR/pinsight_config.<hash>.txt — content-addressed,
 * write-if-absent, tmp+rename; immediate no-op when the env is unset.
 * CONTROL-THREAD ONLY (start + after each config reload): all manifest file
 * I/O stays off app threads. Design §2.8. */
void pinsight_manifest_dump_config(void);

#endif /* PINSIGHT_MANIFEST_H */
