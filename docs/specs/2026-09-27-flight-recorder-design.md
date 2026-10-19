# flight-recorder — Ultra-Low-Overhead C++ Flight Recorder — Design Spec

**Status:** Approved for implementation (write-plan + build in a separate session).
**Date:** 2026-09-27
**Owner:** rune
**Sibling of:** `rune-broker`, `rune-client-cpp`, `rune-protocol`, `common` (all under `/Users/vradu/Projects/`).

---

## 0. One-paragraph summary

`flight-recorder` is a standalone C++23 header-first library that provides **JFR-style, always-on-capable, sub-10ns-per-event flight recording** for Rune's hot paths. An event is *measured*: it captures a **start stamp** on `begin()` and, on `commit()`, captures the **end stamp** and appends the completed record to a **per-thread, lock-free, zero-allocation ring buffer**. Timestamps come from the **TSC** (`rdtscp`). A separate (off-hot-path) drainer turns the raw per-thread tapes into **per-site latency histograms** and a **Chrome-trace/speedscope timeline**, so we can always answer "what took long?" without ad-hoc A/B benchmarking. It mirrors the philosophy of the TS `cassette` recorder (bounded, zero-garbage, eject-to-report) but is a native implementation built on Rune's shared-nothing / lock-free idioms.

**Primary motivating problem:** Rune's 3-node produce path exhibits a fixed ~16 ms per-round-trip latency that is invisible to app-idle and driver-idle tuning. flight-recorder must let us instrument the produce→ack path (`IngressRecv → GatewayHandle → WorkerEnqueue → WorkerAppend → StorageWrite → EgressClaim → EgressSend → ClientAckRecv`) and see, in one run, which hop owns the 16 ms.

---

## 1. Goals & non-goals

### Goals
- **JFR begin/commit semantics.** `ev.begin()` stamps start; `ev.commit()` stamps end and enqueues `{site, start, end, corr}`. Nothing touches the ring until `commit()`.
- **Extreme low overhead.** Target **< 10 ns** per committed event (≈ one `rdtscp` + one store). No allocation, no locks, no syscalls on the hot path. Overhead is **benchmark-validated** (per the Rune ULL engineering standard), not assumed.
- **Always-on capable.** Bounded per-thread ring, overwrite-oldest mode, so recording can run continuously in production with fixed memory.
- **Shared-nothing.** One ring per thread; a thread writes only its own ring. No cross-thread atomics on the write path (mirrors Rune's shared-nothing law).
- **Zero-cost when disabled.** Compile-time off → macros expand to nothing. Compile-time on → a single cold-predicted runtime branch allows enable/disable and per-category masking.
- **Cross-thread stitching.** A correlation id (Rune's produce `correlationId`) is carried in each event so one request's spans across gateway→worker→egress **threads** can be reassembled into a single timeline.
- **Actionable output.** Per-site histograms (p50/p99/p999/max/count) and a Chrome-trace/speedscope JSON timeline; optional raw binary tape for offline analysis.
- **Reusable.** Consumed by both `rune-broker` and `rune-client-cpp` (and anything else) via Conan, exactly like the other sibling libs.

### Non-goals
- Not a dynamic string-tracing framework: event *sites* are statically registered; the hot path never formats strings.
- Not network/distributed tracing: correlation is in-process cross-thread. (Extending corr ids across the wire is a possible future, out of scope here.)
- Not a sampler/profiler replacement (perf/VTune): this is deterministic span instrumentation at named sites.
- No Conan `create`/`export`/`upload` performed by the implementing agent — packaging is defined here, but building/publishing the package is the user's CI responsibility (house rule).

---

## 2. Core concepts

### 2.1 Event sites (static registry)
Every measurement point is a statically-declared **site** with a stable numeric id and metadata (name, category). The hot path carries only the `siteId` (a `uint16`); names are resolved at report time.

```cpp
// flight_recorder/Sites.h  (generated or hand-maintained; ONE source of truth)
enum class Site : uint16_t {
    None = 0,
    // --- produce path (category = Produce) ---
    IngressRecv, GatewayHandle, WorkerEnqueue, WorkerAppend,
    StorageWrite, EgressClaim, EgressSend, ClientAckRecv,
    // ... grows as we instrument more paths ...
    Count
};

struct SiteMeta { const char* name; Category category; };
inline constexpr SiteMeta kSiteMeta[] = { /* indexed by Site */ };
```

`Category` is a small enum (`Produce`, `Replication`, `Read`, `Consensus`, `Storage`, `Egress`, …) used for per-category enable masks and grouping in reports.

### 2.2 Event record (the thing stored in the ring)
Recommended 24-byte POD (finalize during implementation; document the choice):

```cpp
struct FrRecord {          // 24 bytes, 8-byte aligned
    uint64_t startTsc;     // TSC at begin()
    uint64_t endTsc;       // TSC at commit(); == startTsc for an instant/mark
    uint32_t corr;         // correlation id (0 = none)
    uint16_t site;         // Site
    uint16_t arg;          // small per-site payload (e.g. partition, byte-bucket); 0 if unused
};
```
- `threadId` is **implicit** — each ring is single-thread, so the thread id is stored once in the ring header, not per record.
- Storing absolute `startTsc`+`endTsc` (not just duration) lets the exporter place spans on a shared timeline for flamegraphs. Duration = `endTsc - startTsc`.
- Tradeoff to evaluate: a 16-byte variant `{startTsc:u64, durTsc:u32, corr? , site:u16, arg:u16}` halves memory but caps duration to ~u32 cycles and drops absolute end. **Recommendation: 24-byte for v1** (clarity + timeline); revisit if memory pressure shows up.

### 2.3 Measured events (JFR begin/commit) — the central API
```cpp
namespace flight_recorder {

class Event {
public:
    // Construct bound to a site. Does NOT stamp yet (cheap; can be created cold).
    explicit Event(Site s) noexcept;

    Event& begin() noexcept;          // stamp startTsc (rdtscp)
    Event& corr(uint32_t c) noexcept; // attach correlation id (fluent)
    Event& arg(uint16_t a) noexcept;  // attach small payload (fluent)

    void commit() noexcept;           // stamp endTsc, append {site,start,end,corr,arg}
                                      // to this thread's ring. No-op if !enabled(site).
};

} // namespace flight_recorder
```

Usage (JFR-style, exactly as requested):
```cpp
flight_recorder::Event ev(Site::WorkerAppend);
ev.begin().corr(correlationId);
// ... work being measured ...
ev.commit();   // end stamp taken here; record added to the buffer on commit
```

Two conveniences built on top:

```cpp
// RAII scoped span: begin() in ctor, commit() in dtor.
#define FLIGHT_RECORDER_SPAN(site, corr) flight_recorder::ScopedSpan _fr_##__LINE__{ (site), (corr) }

// Instant/point event (start == end): records a single timestamped mark.
#define FLIGHT_RECORDER_MARK(site, corr)  flight_recorder::mark((site), (corr))
```

When `FLIGHT_RECORDER` is compiled out, `Event`, `FLIGHT_RECORDER_SPAN`, and `FLIGHT_RECORDER_MARK` all expand to nothing (empty inline / no-op struct). When compiled in but disabled at runtime, `begin()`/`commit()` short-circuit on a single relaxed-load branch.

### 2.4 Timestamp source
- Primary: **`rdtscp`** (serializing enough for ordering within a thread; pair reads to fence if needed). Detect **invariant TSC** (`CPUID` leaf `0x80000007` EDX bit 8) at startup.
- **Calibration:** once at startup, measure TSC ticks over a known `CLOCK_MONOTONIC_RAW` interval → `tscPerNs`. Store in a global; convert cycles→ns only at report time.
- **Fallback:** if no invariant TSC (or non-x86 — note ARM uses `cntvct_el0`), fall back to `clock_gettime(CLOCK_MONOTONIC_RAW)`. Provide a compile/runtime switch. (The VM is x86_64; ARM path is for macOS/CLion dev builds — may be stub/fallback.)
- Guard against per-core TSC skew: events are compared **within a request's timeline** post-hoc; document that cross-core absolute ordering assumes invariant + synchronized TSC (true on modern single-socket boxes). For histograms (per-site duration) only same-thread start/end is used, so skew is irrelevant there.

### 2.5 Storage: per-thread ring
- **Thread-local recorder**, lazily registered on first event from a thread. Registration adds the ring to a global intrusive list/array (guarded by a mutex **only at register/deregister time**, never on the write path).
- Ring = fixed power-of-two capacity of `FrRecord`, masked index. Two modes:
  - **Overwrite-oldest** (always-on production): writer never blocks; oldest records are lost first.
  - **Stop-when-full** (bounded session capture): writer stops recording once full (until drained/reset).
- Ring header: `{ threadId (OS tid / name), capacity, writeIndex, mode, category-mask }`.
- Reuse or adapt `common`'s `O2ORingBuffer` semantics if convenient, but a purpose-built POD ring is simpler here (fixed record size, single producer = the owning thread, single consumer = the drainer reading a snapshot of the write index). The drainer reads lock-free via an acquire-load of the published write index.

### 2.6 Enable / disable / masking
- `FLIGHT_RECORDER` (CMake option / compile define): master compile switch. Off → all macros/`Event` are no-ops with zero code emitted.
- Global `std::atomic<bool> g_enabled` (relaxed loads on hot path) for runtime on/off.
- `std::atomic<uint32_t> g_categoryMask` — enable/disable per `Category` so we can record just the produce path.
- `enabled(site)` = `g_enabled && (g_categoryMask & bit(categoryOf(site)))` — a couple of relaxed loads + a branch, predicted not-taken when off.

---

## 3. Consumption / reporting (off hot path)

A `Recorder` singleton owns the registry of per-thread rings and provides:

1. **`dumpHistograms(out)`** — for each site, aggregate durations across all thread rings into an HdrHistogram-style summary: `count, p50, p90, p99, p999, max, mean` (in ns). Human-readable table. This is the day-one tool for the 16 ms.
2. **`exportChromeTrace(path)`** — emit Chrome Trace Event JSON (`ph:"X"` complete events with `ts`/`dur` in µs, `pid`=process, `tid`=thread name, `name`=site, optional `args:{corr}`) → open in `chrome://tracing`, Perfetto UI, or speedscope. Gives a visual flamegraph/timeline.
3. **`stitchByCorr(corr)`** — gather all events with a given correlation id across threads, sort by `startTsc`, print a per-request waterfall (site, offset-from-first, duration). This is how a single slow produce is dissected.
4. **`writeTape(path)` / `readTape(path)`** — raw binary dump of all rings + a header (tscPerNs, site table, thread names) for offline post-processing (cassette-style eject). Keep the format versioned.
5. **Trigger capture (v2+):** a lightweight end-to-end latency check (e.g., the client or gateway marks request begin/end); when a request exceeds a threshold, call `snapshot()` to freeze current ring contents to a tape → captures exactly the slow outliers (JFR threshold-event analog).

Drainer runs on demand (signal handler, keypress, RPC, or benchmark teardown). It never runs on a hot-path thread.

---

## 4. Overhead budget & validation (ULL standard — mandatory)

- **Target:** `commit()` of an enabled event ≤ 10 ns; disabled (compiled-in, runtime-off) ≤ 1 ns (one predicted branch); compiled-out = 0.
- Provide a **microbenchmark** (`bench/fr_overhead_bench.cpp`, Google Benchmark) measuring: compiled-out, runtime-disabled, and enabled `SPAN`/`MARK` cost; report ns/op.
- Provide a **contention/throughput test**: N threads each committing M events → verify no cross-thread interference (per-thread rings) and aggregate events/sec.
- Acceptance gate: enabled overhead within target on the reference VM; if not, iterate before declaring done. **Do not report done without the microbench numbers** (house rule: never hand over untested code; benchmark-validate every optimization).

---

## 5. Build & packaging

- **Repo:** `/Users/vradu/Projects/flight-recorder` (new). `git init`; origin under `bitbucket.org:jcodeteam/flight-recorder.git` (match sibling naming; user creates the remote).
- **Language/std:** C++23, little-endian assumption documented (matches Rune). Header-first with a small `.cpp` for the recorder/registry/calibration.
- **CMake:** target `flight_recorder` (INTERFACE or STATIC). Option `FLIGHT_RECORDER` (default ON). Must **configure on both macOS (CLion) and Linux** — no hard x86 requirement at configure time; the ARM/TSC-fallback path keeps macOS builds green (mirror the aeron-recipe portability lesson: never break `cmake` configure on macOS/armv8).
- **Conan:** provide a `conanfile.py` so `rune-broker` / `rune-client-cpp` consume it as a normal dependency. The implementing agent writes the recipe but does **not** run `conan create/export/upload` — the user's CI builds/publishes.
- **Dependencies:** standard library only for core. Google Benchmark + a test framework (GoogleTest, matching the other repos) for tests/benches. HdrHistogram: either vendor a tiny header-only percentile accumulator or implement a simple log-bucketed histogram to avoid a heavy dep.

---

## 6. Integration surface (how Rune will call it — v1 wiring is a follow-up)

The recorder is generic; Rune adds `#include <flight_recorder/FlightRecorder.h>` and drops spans at sites. Example (broker worker append):
```cpp
void Worker::applyProduce(const ProduceOp& op) {
    FLIGHT_RECORDER_SPAN(flight_recorder::Site::WorkerAppend, op.correlationId);
    ...
}
```
Instrumentation of the actual Rune sites is **out of scope for the flight-recorder build session** — the flight-recorder session delivers the library + tests + benches. A later session (back in the broker worktree) will place the measures and hunt the 16 ms.

---

## 7. Milestones (suggested execution order for the separate session)

Use `superpowers:writing-plans` to turn these into bite-sized TDD tasks.

- **P0 — Repo scaffold.** `git init`, CMake skeleton, C++23, `FLIGHT_RECORDER` option, GoogleTest + Google Benchmark wired, CI-less local build green on macOS + Linux.
- **P1 — TSC + calibration.** `rdtscp` wrapper, invariant-TSC detection, `CLOCK_MONOTONIC_RAW` calibration → `tscPerNs`, cycles↔ns helpers, fallback path. Tests: calibration sanity (duration of a known sleep within tolerance), monotonicity.
- **P2 — Per-thread ring + record.** `FrRecord` POD, per-thread ring (both modes), thread registration/registry, lock-free published write index. Tests: single-thread fill/overwrite/stop-when-full; multi-thread isolation.
- **P3 — Event API (begin/commit) + macros.** `Event`, `ScopedSpan`, `mark()`, `FLIGHT_RECORDER` compile-out, runtime enable + category mask. Tests: begin/commit produces one record with correct site/corr and `end>=start`; disabled = no record; compiled-out = no symbols.
- **P4 — Reporting.** `dumpHistograms`, `stitchByCorr`, `exportChromeTrace`, `writeTape/readTape`. Tests: histogram percentiles on synthetic data; Chrome-trace JSON schema golden; corr stitch ordering; tape round-trip.
- **P5 — Overhead microbench + throughput test.** Meet the < 10 ns target; publish numbers in the repo README. Gate.
- **P6 — Conan recipe + consumption smoke.** Recipe written; a tiny sample `find_package`/`target_link_libraries` consumer builds. (No publish by the agent.)

Each milestone ends with an independently testable, committed deliverable.

---

## 8. Acceptance criteria (definition of done for the flight-recorder session)
1. `flight_recorder` builds on macOS (CLion) and Linux with `FLIGHT_RECORDER=ON` and `FLIGHT_RECORDER=OFF`.
2. JFR-style `begin()/commit()` records a correct measured event; RAII `FLIGHT_RECORDER_SPAN` and `FLIGHT_RECORDER_MARK` work.
3. Per-thread lock-free rings, zero allocation on the write path (verified by an allocation-counting test or inspection).
4. Enabled-event overhead ≤ 10 ns on the reference VM, with published microbench numbers; compiled-out emits no code.
5. Histogram + Chrome-trace + corr-stitch + tape round-trip all covered by tests.
6. Conan recipe present; a sample consumer links and runs. No `conan create/export/upload` performed by the agent.
7. All tests green; overhead bench green; README documents usage + how to open the Chrome trace.

---

## 9. Open questions to resolve during implementation
- Final record size (16 vs 24 bytes) — pick after the overhead microbench; document the choice.
- HdrHistogram: vendored micro-impl vs simple log-bucketed — pick the lighter that gives p99/p999 with acceptable error (< 1%).
- Ring capacity default (per thread) and total memory budget for always-on mode — pick a default (e.g., 1–4 MiB/thread = 64k–256k records) and make it configurable.
- ARM/macOS timestamp path: `cntvct_el0` vs `clock_gettime` fallback — fallback is acceptable for dev builds (the hot-path target is the x86 VM).
