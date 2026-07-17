# Call-path profiling for PInsight — HPCToolkit study & adoption design

**Status:** study + design · 2026-07-14
**Scope:** (I) how HPCToolkit achieves *low-overhead* call-path profiling — the
overhead-reduction mechanisms and how `hpcrun` + `hpcstruct` divide the work to
recover call paths; (II) a design to adopt/adapt that approach in PInsight for
low-overhead call-path **tracing and profiling**.
**Related:** [rate-limit-tracing.md](rate-limit-tracing.md),
[architecture](code-memory/architecture.md); PInsight backtrace code:
[src/backtrace.c](../src/backtrace.c), [src/backtrace.h](../src/backtrace.h).

> **Why call paths.** Flat/lexgion profiling attributes cost to a region by its
> code address. When the *same* region is reached through different **call paths**
> (e.g. a helper `h()` containing an instrumented call, invoked from `foo()` vs
> `bar()`), flat profiling cannot separate them. Call-path profiling attributes
> cost to the *full calling context*. PInsight's lexgion stack captures only
> **region nesting** (an `omp for` inside an `omp parallel`), **not** the function
> call path — so true call-path support needs call-stack unwinding.

---

# Part I — How HPCToolkit does low-overhead call-path profiling

## 1. The problem it solves
Attribute performance to full dynamic calling context in **unmodified, fully
optimized** binaries (no source, no frame pointers, no recompilation), at **low,
controllable overhead** (reported **2–7%** in csprof/ICS'05; **1–5%** in mature
HPCToolkit). The naive approaches fail: per-call-body instrumentation (`gprof`)
explodes on call-intensive code, and a full stack unwind on every event is far too
expensive.

## 2. The four pillars

1. **Sampling, not instrumentation (decouple cost from call frequency).**
   Measurement is driven by **asynchronous samples** — an interval timer (SIGPROF)
   or a hardware performance-counter overflow trap. On each sample, `hpcrun` takes
   **one** stack unwind. Cost is therefore proportional to the **sample rate** (a
   knob), *independent of how many calls the program makes*. This is the
   foundational choice.

2. **Store paths as a Calling Context Tree (CCT), inserted incrementally.**
   Samples fold into a CCT rather than a list of raw stacks. Each frame is keyed by
   its **(instruction pointer, stack pointer)** so recursion is disambiguated. The
   deepest node's counter is bumped per sample. Insertion is incremental (see #3).

3. **Incremental unwinding — the crux of *low* overhead.**
   *"All procedure activations that existed when the previous sample was taken are
   already known, so there is no reason to unwind through these."* The profiler
   keeps the previous sample's per-frame CCT-node pointers and, on a new sample,
   finds the **deepest frame where the new and previous stacks agree**, then walks
   and inserts only the **changed suffix**. Two ways to detect the common frame:
   - **Sample bit** (Whaley): a flag per activation (packed in a low return-address
     bit); walk marks frames until it hits an already-marked one.
   - **Trampoline** (Arnold & Sweeney): replace the marked frame's **return address
     with a trampoline** (original saved); when that frame returns, control diverts
     to the trampoline, which pops one node off a cached **"shadow stack"** (each
     shadow node → its CCT node). Only **one trampoline in the stack at a time**;
     it re-installs above the interrupted procedure each sample.

   Net: amortized unwind/insert cost ∝ **stack *change***, not stack *depth*.
   (Samples landing while the trampoline runs are "unsafe" — <0.1% in practice.)

4. **Binary-analysis unwinding of optimized code (no frame pointers).**
   To unwind at an arbitrary PC — including inside prologues/epilogues where a
   frame pointer may not be set up — `hpcrun` computes **unwind recipes** by a
   **linear scan of a procedure's machine instructions**, building **intervals**:
   each instruction range gets a recipe stating *where the return address / caller
   frame currently live* (a register, or a stack offset), and a new interval starts
   whenever an instruction changes the recipe (`push`/`pop` moving SP, a save of
   RA, …). Recipes are **cached per procedure**. This needs **function bounds**
   (start/end of each routine) first — recovered by binary analysis because
   stripped libraries omit them. It handles embedded jump-table data (which can
   corrupt decoding) and runtime `dlopen` (via **"epochs"** — a CCT is tied to the
   loaded-module epoch so an address reused by a later library is attributed
   correctly).

## 3. Overhead-reduction mechanisms (summary)

| Mechanism | What it removes |
|---|---|
| Sampling (vs instrumentation) | cost ∝ sample rate, **not** call rate |
| Incremental unwind (shadow stack) | re-walking unchanged deep frames every sample → walk only the changed suffix |
| Cached unwind recipes | recomputing per-PC unwind info on every unwind |
| CCT (vs raw sample list) | storage blow-up; enables prefix sharing |
| Offline structure recovery | moves the expensive symbolization/attribution off the hot path entirely |

## 4. `hpcrun` — the runtime measurement subsystem
Launched via `LD_PRELOAD` on the unmodified binary. It: installs sample sources
(timer/PMU); on each sample **unwinds** using the on-the-fly interval recipes
(with libunwind on some arches — the HPCToolkit team **upstreamed recipe caching
into libunwind** specifically so an external tool can cache per-procedure
recipes); recovers **function bounds** by its own analysis; folds the path into the
per-thread **CCT**; and handles dynamic loading via epochs. It records IPs, **not**
source info — symbolization is deferred. It **does not instrument** the binary.

## 5. `hpcstruct` — offline static structure recovery
Run on the application binary + libraries, **independently of (usually after) the
run** — *not* a pre-run instrumentation step. It parses machine instructions,
**reconstructs the control-flow graph**, and combines **DWARF line maps** with
**interval analysis on the CFG** to recover **procedures, loop nests** (cycles in
the CFG), **inlined functions/templates**, and **source-line mappings**, producing
a "program structure" file. Built on **Dyninst** (ParseAPI for CFG/parsing,
SymtabAPI for symbols/line-maps/DWARF). Purpose: **attribution** — map measured PCs
back to source-level constructs even under heavy optimization (inlining, loop
fusion, scalarization) and even without source.

## 6. How they work together (division of labor)
```
run time:   hpcrun  ──(sample + unwind + CCT of IPs)──►  measurement DB (IPs, metrics)
offline:    hpcstruct ──(binary analysis: CFG, loops, inlining, lines)──►  program structure
            hpcprof   ──(correlate measurement IPs × structure)──────────►  performance DB
            hpcviewer ──(browse: flat / callers / callees / callpath)
```
The key idea: **the runtime path is deliberately minimal** — capture IPs into a CCT
via cheap incremental unwinding — while the **expensive work (symbolization, loop/
inline recovery, source correlation) is done offline**. That split is what keeps
runtime overhead at a few percent.

## 7. No binary instrumentation — the defining choice
HPCToolkit measures **unmodified, optimized** binaries. Binary analysis is used to
**read/understand** the binary (to unwind, and to attribute), **never to rewrite**
it. csprof (ICS'05): the stack is captured *"without any instrumentation of the
[application]."* The one nuance: the csprof-era **trampoline** dynamically
overwrites a *return address on the stack* at runtime — that is **dynamic stack
manipulation, not static binary instrumentation**, and it is an optional
optimization; the mature path relies on cached interval-based recipes.

## 8. References
- [Froyd, Mellor-Crummey & Fowler — *Low-Overhead Call Path Profiling of Unmodified, Optimized Code*, ICS'05](https://www.cs.rice.edu/~johnmc/papers/csprof-ics05.pdf) — sampling, CCT, sample-bit/trampoline shadow stack, procedure descriptors.
- [Tallent, Mellor-Crummey & Fagan — *Binary Analysis for Measurement and Attribution of Program Performance*, PLDI'09](https://www.cs.rice.edu/~johnmc/papers/hpctoolkit-pldi-2009.pdf) — the interval-based unwind-recipe algorithm and attribution.
- [`hpcstruct` man page — Recovery of Static Program Structure](https://hpctoolkit.org/man/hpcstruct.html) — Dyninst, CFG, loops, inlining.
- [Adhianto et al. — *HPCToolkit: Tools for performance analysis of optimized parallel programs*, CCPE 2010](https://www.cs.umd.edu/class/spring2021/cmsc714/readings/Adhianto-hpctoolkit.pdf) — system overview, 1–5% overhead.
- [HPCToolkit publications](https://hpctoolkit.org/publications.html).

---

# Part II — Adopting & adapting the approach in PInsight

## 9. The architectural difference (read this first)
HPCToolkit is a **sampler** — it samples *because it has no other way to know when
something interesting happened*. **PInsight is an event-driven tracer**: OMPT /
PMPI / ROCTracer callbacks already deliver control at each region begin/end. So
PInsight **never needs sampling to decide *when***; what it needs is the **call
path cheaply at each event**. Therefore the transferable ideas are **not**
"sampling," but the mechanisms that make the *unwind itself* cheap and the *storage*
compact.

## 10. Current state in PInsight and why it is too costly
[src/backtrace.c](../src/backtrace.c) `retrieve_backtrace()` calls glibc
`backtrace()` into a 32-deep `__thread` IP array; [src/backtrace.h](../src/backtrace.h)
attaches it to **every** record as a variable-length hex sequence. Gated by
`PINSIGHT_BACKTRACE` (default `FALSE`), wired into OMPT/CUDA/HIP callbacks (not
MPI/Python). Problems, in HPCToolkit terms:
1. **Full unwind, every event** — glibc `backtrace()` parses `.eh_frame` **per
   frame** (no recipe caching) → µs-scale, synchronous, on the app thread. This is
   exactly the "no reason to unwind through unchanged frames" waste that pillar #3
   eliminates, made worse by no recipe caching (pillar #4).
2. **Raw IPs, every record** — up to 32×8 = 256 B/record, dwarfing the event and
   inflating the LTTng ring buffers (bad for the streaming path too).
3. **Runtime symbolization** in the debug path (`backtrace_symbols()`) — correctly
   kept debug-only, but underscores that symbolization must be **offline**.

## 11. What transfers, what to adapt, what to drop

| HPCToolkit mechanism | PInsight action |
|---|---|
| Sampling to decide *when* | **Drop** — PInsight is event-driven; instead **sample which events get a path** using existing rate control (§12.6). |
| Incremental unwind via shadow stack | **Adopt** — persist a per-thread shadow stack across events; unwind only the changed suffix. |
| Trampoline (return-address rewriting) | **Drop** — invasive/fragile across OMPT/ROCm/MPI runtimes, signals, `longjmp`, tail calls. Use the **non-trampoline** variant: compare `(SP, RA)` to the cached shadow stack to find the common prefix. |
| Binary-analysis unwind recipes + caching | **Adopt via libunwind** (with the HPCToolkit-contributed **recipe cache**) instead of glibc `backtrace()`. |
| CCT with prefix sharing | **Adopt** — build an interned CCT; records carry an **8-byte `cct_node_id`**, not raw IPs. |
| Offline structure recovery (`hpcstruct`/Dyninst) | **Adopt lightweight** — offline symbolization with `addr2line`/DWARF (optionally Dyninst) in the analysis stage; never at runtime. |
| Epochs for `dlopen` | **Adopt** — stamp `cct_node_id`s with a load-map generation; re-emit node defs after `dlopen`. |

## 12. Proposed design

### 12.1 Data structures (per thread, TLS — no locking)
```c
typedef struct { void *sp; void *ra; uint32_t cct_node; } shadow_frame_t;
__thread shadow_frame_t shadow[MAX_DEPTH];   // cached prefix of the last event
__thread int            shadow_len;
// Interned CCT (per process or per thread): node = (parent_id, ip) -> id.
// A hash map (parent_id, ip) -> cct_node_id, plus a table id -> (parent_id, ip).
```

### 12.2 The incremental unwind (per event, replaces `retrieve_backtrace()`)
```
cct_node_id get_callpath_context():
    walk the native stack top-down with a FAST stepper (libunwind, cached recipes),
    producing (ip, sp) frames, BUT stop as soon as (sp,ra) matches shadow[k]
      → frames below k are unchanged; reuse shadow[k].cct_node as the prefix.
    intern the changed suffix into the CCT (parent = reused node), updating `shadow`.
    return the id of the deepest (leaf) CCT node.
```
Cost is **O(changed frames)** amortized, not O(depth); no `.eh_frame` re-parse (cached
recipes); no return-address rewriting.

### 12.3 Hook points
Replace each `#ifdef PINSIGHT_BACKTRACE retrieve_backtrace();` in the OMPT / CUPTI /
ROCTracer callbacks with `cct = get_callpath_context();`. Add MPI/Python later.

### 12.4 Trace-record change
Replace `LTTNG_UST_TP_FIELDS_BACKTRACE` (the 256-B sequence) with a single
`lttng_ust_field_integer(uint32_t, cct_node, cct_node)`. **~32× smaller per record.**

### 12.5 CCT node-definition events (make the trace self-describing)
Each time a **new** CCT node is interned, emit it **once** on a dedicated
tracepoint: `cct_node_def { id, parent_id, ip, load_epoch }`. The trace then carries
the tree incrementally (works for live streaming too); offline reconstruction joins
`cct_node` on records to the node-def stream. No per-record IP storage.

### 12.6 Integration with PInsight's rate control (extra savings HPCToolkit lacks)
Because call paths are stable across a region's invocations, capture/refresh the
path only on:
- the **first trace of each lexgion** (attach that `cct_node` to subsequent
  invocations of the same lexgion until reset), and/or
- **1-in-N** via `tracing_rate` / `window_end_trigger`.

This makes native unwinding a **per-region-instance** (or sampled) cost, not a
per-event cost — a strictly cheaper regime than HPCToolkit's per-sample unwind.

### 12.7 Offline reconstruction & views
Analysis stage (babeltrace2 → Python, extending the existing `analyze_*` pattern):
join records to `cct_node_def`, rebuild the CCT, **symbolize IPs offline**
(`addr2line`/DWARF against the binary + build-id; optionally Dyninst/`hpcstruct`-style
for loops/inlining), and offer **flat vs call-path** views (the whole point:
distinguish the same lexgion under different paths). This mirrors HPCToolkit's
`hpcstruct`+`hpcprof` split.

### 12.8 GPU consideration
The above yields the **host** call path down to a kernel launch (`hipLaunchKernel`),
which is the high-value target (attribute GPU work to the host context that
launched it, via the existing `correlation_id`). **GPU-side** calling context is a
separate, harder problem (Rice's later GPU-CCT work) — out of scope for a first cut.

## 13. Overhead model
- **Per event:** one fast stepper walk of the *changed* frames + a hash lookup +
  (rarely) a new-node emit. Target: tens–hundreds of ns amortized vs the current
  µs-scale full DWARF unwind.
- **Per record:** 4 B `cct_node` vs 256 B raw IPs.
- **One-time:** one `cct_node_def` per unique node (bounded by the size of the CCT,
  not the number of events).
- With §12.6 sampling, per-*event* unwind cost approaches zero for hot regions.

## 14. Phased implementation plan
1. **Unwinder swap (biggest single win):** replace glibc `backtrace()` with
   **libunwind + cached recipes**; keep per-event capture initially. Measure.
2. **Incremental shadow stack:** add the per-thread `(sp,ra,cct_node)` cache and the
   common-prefix stop; unwind only the suffix.
3. **Interned CCT + `cct_node` field + `cct_node_def` events:** replace the raw-IP
   sequence field; move symbolization offline.
4. **Rate-control gating (§12.6):** first-encounter / 1-in-N path capture.
5. **Offline tooling:** CCT reconstruction + `addr2line` symbolization + flat/
   call-path views in the analysis scripts.
6. **Extend domains:** wire MPI (PMPI) and Python; consider host-path-to-kernel for
   HIP/CUDA via `correlation_id`.

## 15. Risks & open questions
- **libunwind availability/robustness** on Tuolumne (Cray/ROCm stacks): verify
  unwinding through `libmpi_cray`, `libamdhip64`, `libomp`; corrupt-interval / jump-
  table cases (HPCToolkit found these rare but real). Fallback: glibc `backtrace()`.
- **Async-safety / re-entrancy:** unwinding from arbitrary callbacks (OMPT/ROCTracer
  internals on the stack) is generally safe but adds noise frames to trim offline;
  ensure no unwinder locks deadlock with the traced runtime.
- **Shadow-stack invalidation:** `longjmp`/`setjmp`, C++ exceptions, and tail calls
  can silently pop frames the non-trampoline scheme won't notice; mitigate by
  validating the cached prefix against the live stack (SP monotonicity) each event.
- **`dlopen` epochs:** stamp and re-emit node defs on load-map change.
- **CCT growth:** bound memory; per-thread vs shared CCT (shared needs locking — a
  per-thread CCT with offline merge is simpler and lock-free).
- **Interaction with existing `codeptr_ra`:** PInsight already records the immediate
  call site per event; the CCT adds the *ancestor* dimension. Decide whether the
  leaf CCT node is the `codeptr` (dedupe) or a synthetic runtime-call frame.
