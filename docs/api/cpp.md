# C++ API

Namespace `flight_recorder`. Include `<flight_recorder/FlightRecorder.h>`; it pulls in every public header (`Config.h`,
`Version.h`, `Sites.h`, `Keys.h`, `FrRecord.h`, `Tsc.h`, `Calibration.h`, `Enable.h`, `Recorder.h`, `Event.h`,
`Categories.h`, `Histogram.h`, `Report.h`). Every example here was compiled and run against these headers.

## The master switch

`FLIGHT_RECORDER` is `1` (compiled in) or `0` (compiled out). The build defines it; when a consumer includes the headers
without the define it defaults to `1` (`Config.h`). Compiled out, `Event`, `ScopedSpan`, `SlowSpan`, `Throttle`, `mark`,
`markVal`, `markArgVal` and every macro are empty bodies, so no recorder symbol is referenced and nothing is linked.
`Histogram`, `Keys.h` and the report functions remain available either way.

## Sites and categories

```cpp
enum class Category : uint8_t { Produce, Replication, Read, Consensus, Storage, Egress, Runtime, Count };
enum class Site : uint16_t { None = 0, IngressRecv, GatewayHandle, /* ... 146 sites */ Count };
const char* nameOf(Site) noexcept;       // "WorkerAppend"
Category    categoryOf(Site) noexcept;   // Category::Produce
```

The registry is static: one `Site` enumerator and one `kSiteMeta` entry (name, category) per measurement point, kept in
lockstep by a `static_assert`. To add a site, add the enumerator and the meta entry in `Sites.h`. The list is in
[sites.md](../sites.md).

## Recording

### `Event`: begin and commit

```cpp
flight_recorder::Event ev(Site::WorkerAppend);
ev.begin().corr(correlationId).arg(2).value(3.5);   // chainable setters
// ... the work ...
ev.commit();                                        // stamps the end, appends one record
```

| Member | Meaning |
|---|---|
| `Event(Site)` | Cheap: fills the record, records nothing yet. |
| `Event& begin()` | Checks `enabled(site)` **once**, and if on, stamps the start timestamp. Disabled path: two relaxed loads and a branch. |
| `Event& corr(uint32_t)` | Correlation key used to stitch one request across threads (0 = none). |
| `Event& arg(uint16_t)` | A small per-site payload (reason, worker, partition). |
| `Event& value(double)` | A free-form value (latency in µs, queue depth, batch bytes). |
| `void commit()` | If `begin()` was enabled: stamps the end timestamp and pushes the record. No-op otherwise. |
| `void commitIfAtLeast(uint64_t minTsc)` | Commits only if the span lasted at least `minTsc` ticks. (Compiled-in builds only.) |

An `Event` is a stack object owned by one thread. Nothing touches the ring until `commit()`.

### Marks

An instant: start equals end.

```cpp
flight_recorder::mark(Site::IngressRecv, corr, /*arg=*/0);
flight_recorder::markVal(Site::ProduceAckSend, corr, 3.5);
flight_recorder::markArgVal(Site::ShPushRefused, corr, /*arg=*/1, 3.5);
```

### Scoped spans

| Type | Behavior |
|---|---|
| `ScopedSpan(Site, corr)` | `begin()` in the constructor, `commit()` in the destructor. |
| `SlowSpan(Site, corr)` | Like `ScopedSpan`, but recorded only if it lasted at least `slowSpanTsc()`. For per-pass spans of a busy-spinning loop where only the outliers matter, so the ring holds a whole run. Disabled cost equals `ScopedSpan`. |
| `Throttle` | A plain member of a single-threaded owner. `hit(Site, corr, arg)` emits at most one mark per `throttleTsc()` ticks; the emitted record's `value` is the number of hits it stands for. For marks that repeat every pass while a condition lasts (a refused offer retried each pass). |

Both `SlowSpan` and `Throttle` do nothing unless their threshold is set: `slowSpanTsc()` and `throttleTsc()` start at `0`
(record everything, no coalescing). `configureFromEnv()` sets them to 2000 ns and 50 000 ns by default; otherwise use
`setSlowSpanTsc(nsToCycles(2000))`.

## Macros

The macros wrap the types above and are the intended call-site API.

| Macro | Expands to |
|---|---|
| `FLIGHT_RECORDER_SPAN(site, corr)` | a scoped span named by `__LINE__` |
| `FLIGHT_RECORDER_MARK(site, corr)` | `mark(site, corr)` |
| `FLIGHT_RECORDER_MARK_VAL(site, corr, value)` | `markVal(...)` |
| `FLIGHT_RECORDER_MARK_ARG_VAL(site, corr, arg, value)` | `markArgVal(...)` |
| `FLIGHT_RECORDER_SLOW_SPAN(site, corr)` | a `SlowSpan` |
| `FLIGHT_RECORDER_THROTTLED(throttle, site, corr, arg)` | `throttle.hit(site, corr, arg)` |

**Use the per-category forms**, which add the category's compile-time switch on top of the master switch. Each category
`<C>` in `PRODUCE`, `REPLICATION`, `READ`, `CONSENSUS`, `STORAGE`, `EGRESS`, `RUNTIME` has all six:

```cpp
FLIGHT_RECORDER_SPAN_PRODUCE(site, corr);
FLIGHT_RECORDER_SLOW_SPAN_RUNTIME(site, corr);
FLIGHT_RECORDER_MARK_REPLICATION(site, corr);
FLIGHT_RECORDER_MARK_VAL_PRODUCE(site, corr, value);
FLIGHT_RECORDER_MARK_ARG_VAL_PRODUCE(site, corr, arg, value);
FLIGHT_RECORDER_THROTTLED_REPLICATION(throttle, site, corr, arg);
```

The suffix **must** match the site's category (`categoryOf(site)`): it selects which compile flag gates the call.
`-DFLIGHT_RECORDER_NO_<CATEGORY>` turns that category's macros into `do {} while (0)`.

```cpp
struct Worker {
    flight_recorder::Throttle refused;            // one per call site, owned by one thread
    void pass(uint32_t corr) {
        FLIGHT_RECORDER_SPAN_PRODUCE(Site::WorkerAppend, corr);
        FLIGHT_RECORDER_MARK_VAL_PRODUCE(Site::ProduceAckSend, corr, 42.0);
        FLIGHT_RECORDER_THROTTLED_REPLICATION(refused, Site::ShPushRefused, corr, /*arg=*/1);
        FLIGHT_RECORDER_SLOW_SPAN_RUNTIME(Site::WorkerDoWork, 0);   // kept only if >= slowSpanTsc()
    }
};
```

## Enabling and gating (`Enable.h`)

```cpp
void enable() noexcept;   void disable() noexcept;   bool isEnabled() noexcept;     // the master runtime flag, default off
void setCategoryMask(uint32_t) noexcept;   uint32_t categoryMask() noexcept;          // default 0xFFFFFFFF (all)
constexpr uint32_t categoryBit(Category) noexcept;                                    // 1u << category
bool enabled(Site) noexcept;                                                          // master flag and category bit
void setSlowSpanTsc(uint64_t) noexcept;   uint64_t slowSpanTsc() noexcept;
void setThrottleTsc(uint64_t) noexcept;   uint64_t throttleTsc() noexcept;
```

The flags are relaxed atomics: a site that is disabled costs a load and a branch. A recording is off until something calls
`enable()` (directly, or through `configureFromEnv()`).

## Time (`Tsc.h`, `Calibration.h`)

Timestamps are raw ticks, converted to nanoseconds only at report time.

| Function | Meaning |
|---|---|
| `uint64_t rdtscp()` | The tick counter: `rdtscp` on x86_64, `cntvct_el0` on arm64, `CLOCK_MONOTONIC_RAW` nanoseconds otherwise. Ordering holds within a thread. |
| `void calibrate()` | Measures ticks per nanosecond once (idempotent): a 10 ms window on x86_64, `cntfrq_el0` on arm64, `1.0` on the fallback. |
| `double tscPerNs()` | The calibrated rate. |
| `double cyclesToNs(uint64_t)`, `uint64_t nsToCycles(double)` | Conversions. |
| `uint64_t counterFrequencyHz()`, `bool hasInvariantTsc()` | Platform facts. Cross-core ordering assumes an invariant, synchronized TSC (true on modern single-socket machines). |

Call `calibrate()` (or `configureFromEnv()`, which does) before the first report, or the rate is `1.0`.

## The recorder (`Recorder.h`)

| Member | Meaning |
|---|---|
| `Recorder::instance()` | The process singleton. |
| `configure(size_t payloadCapacityPow2)` | Ring size for threads that register **afterward**. Any value is floored to 256 bytes and rounded up to a power of two. The default is 16 MiB; `configureFromEnv()` uses 128 MiB. Call it before the recording threads start. |
| `registerThread()`, `snapshotThreads()` | Registration and a copy of the thread list. A thread registers on its first record. |
| `drain()` | The single drainer: moves every committed record from every ring into that thread's tape. Off the hot path; safe to call before each report. |
| `drainAndView()` | Drains, then returns a `TapeView` that **holds the registry lock** for its lifetime so the background drainer cannot trim a tape while you walk it. `view.threads()` gives each `ThreadRecorder` (`drained` records, `osTid`, `threadName`, `dropped`). Do not call another locking `Recorder` method while holding one. |
| `reset()` | Quiescent or test use only: retires every ring to a graveyard (so a producer's raw pointer stays valid memory); each thread re-registers a fresh ring on its next record. |
| `generation()` | Bumped by `reset()`. |

Free functions: `ThreadRecorder* threadRecorder()` (first call registers the thread, then a thread-local load),
`void pushRecord(const FrRecord&)` (a full ring drops the record and counts it), `uint64_t droppedRecords()` (total drops),
`void startBackgroundDrain(unsigned periodMs)` and `void stopBackgroundDrain()` (a recorder-owned thread; idempotent;
stopped at exit).

The **tape** is bounded: each thread keeps its newest `FLIGHT_RECORDER_TAPE_RECORDS_PER_THREAD` records (default 2 097 152,
48 MiB); the drainer trims the oldest in bulk once a tape passes that cap by 25%. Histograms are built from the tape when you ask for them, so a long run's percentiles reflect the retained window.

### The record

```cpp
struct FrRecord {                 // 32 bytes, 8-byte aligned, little-endian hosts only
    uint64_t startTsc;            // ticks at begin()
    uint64_t endTsc;              // ticks at commit(); equal to startTsc for a mark
    uint32_t corr;                // correlation key, 0 = none
    uint16_t site;                // Site
    uint16_t arg;                 // small per-site payload
    double   value;               // free-form value
    uint64_t durationTsc() const noexcept;   // never underflows
};
```

## Reports (`Report.h`)

| Function | Output |
|---|---|
| `bool configureFromEnv()` | Lifecycle helper: sizes the rings, applies the categories and thresholds, enables recording and starts the background drain, all from `FLIGHT_RECORDER_*` ([configuration](../configuration.md)). Returns `true` iff recording is on. Call it once, from `main`, before the recording threads start. |
| `vector<SiteStats> collectSiteStats()` | One `{site, count, p50, p90, p99, p999, max, mean}` per site that recorded (ns). |
| `void dumpHistograms(std::ostream&)` | The table of the above. |
| `vector<WaterfallEntry> stitchByCorr(uint32_t corr)`, `void printWaterfall(uint32_t corr, std::ostream&)` | Every span with that correlation key across all threads, ordered by start, with the offset from the first. `corr == 0` is never stitched. |
| `bool exportChromeTrace(const std::string& path)` | A Chrome trace JSON file. |
| `bool writeTape(path)`, `bool readTape(path, Tape&)` | The raw tape (`RUNEFR01`, version 1) and its reader: `Tape{version, tscPerNs, siteNames, threads}`. |
| `RunAnchor runAnchor()` | The earliest start tick and one simultaneous (tick, monotonic, realtime) sample, for aligning processes. |
| `bool dumpAll(dir, suffix, role, node)` | Drains, then writes `<dir>/fr-hist<suffix>.txt`, `fr-trace<suffix>.json`, `fr-tape<suffix>.bin` and `fr-meta<suffix>.json`. Returns `false` when recording is off. The directory must exist. |

File formats: [output-formats.md](../output-formats.md).

### `Histogram`

`flight_recorder::Histogram` is a log-linear (HdrHistogram-style) histogram of non-negative integers, 8 mantissa bits, about
0.78% maximum relative error: `record(v)`, `count()`, `min()`, `max()`, `mean()`, `valueAtPercentile(p)` (the lower bound of
the bucket holding the p-th percentile, `p` in 0 to 100). Exact below 256.

## Correlation keys

`Keys.h` (constexpr, free whether the recorder is compiled in or out) defines the 32-bit keys that chain one batch end to end
across processes: `foldCorr(int64_t correlationId)` folds a client batch id into 32 bits, attempt-free, so every retransmit
shares the key; `partKey(topic, partition, offset)` packs `((topic+8)&0xF)<<28 | (partition&0xF)<<24 | offset&0xFFFFFF`
(internal topics with negative ids down to -8 keep distinct nibbles; an analyzer unwraps the 24-bit offset per series).

## Threading rules

- **Record from any thread.** Each thread writes only its own ring. The first record registers the thread (a lock, once).
- A `Throttle`, `SlowSpan` or `Event` belongs to the thread that uses it; share none of them.
- **One drainer.** `drain()`, the background drainer and every report go through the registry lock; reports are safe to call
  while threads record.
- `reset()` is for quiescent points and tests.
- Rings never block and never allocate on the hot path: a full ring drops and counts. Check `droppedRecords()` (and `dropped`
  in the meta file); a non-zero value means the trace has holes. Raise `FLIGHT_RECORDER_RING_BYTES` or the drain frequency.
