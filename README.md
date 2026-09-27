# rune-fr

Ultra-low-overhead C++23 flight recorder (JFR-style begin/commit spans) for Rune's hot paths.

An event is *measured*: `begin()` stamps a start TSC, `commit()` stamps the end and appends a
completed 24-byte record to a per-thread, lock-free, zero-allocation ring buffer. A separate
off-hot-path drainer turns raw tapes into per-site latency histograms and a Chrome-trace
timeline, so you can always answer "what took long?" without ad-hoc A/B benchmarking.

## Build

```bash
conan install . -of build -s build_type=Release -s compiler.cppstd=23 -o rune-fr/*:rune_fr=True --build=missing
cmake --preset conan-release -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset conan-release -j
ctest --preset conan-release --output-on-failure
```

(For a debug/test cycle, substitute `build_type=Debug` and the `conan-debug` preset.)

### Dependency: aeron (and macOS/armv8)

The per-thread ring (`rune_fr::detail::M2ORingBuffer`) is a namespace-isolated copy of Rune's
agrona M2O ring built on aeron's **header-only** `AtomicBuffer`/`RecordDescriptor` — rune-fr
depends on **aeron**, not on `common`. The aeron ConanCenter recipe rejects macOS/armv8, so
there aeron is skipped (see `conanfile.py`) and CMake provides a stub `aeron::aeron` target so
configure/IDE loading works. To actually build/test on macOS, point at cached aeron headers:

```bash
cmake --preset conan-debug -DBUILD_TESTING=ON \
  -DRUNE_FR_AERON_INCLUDE_DIR=$(dirname $(dirname $(find ~/.conan2 -path '*p/include/concurrent/AtomicBuffer.h' | head -1)))
```

The ring is **drop-on-full**: `commit()` never blocks the hot path; if a thread's ring is full
(drainer not keeping up), the event is dropped rather than overwriting or stalling.

## Usage

```cpp
#include <rune_fr/FlightRecorder.h>

rune_fr::Event ev(rune_fr::Site::WorkerAppend);
ev.begin().corr(correlationId);
// ... work ...
ev.commit();

// or RAII:
RUNE_FR_SPAN(rune_fr::Site::WorkerAppend, correlationId);
```

## Reports

Off the hot path, drain via `rune_fr::Recorder`:
- `dumpHistograms(out)` — per-site p50/p90/p99/p999/max/count/mean (ns).
- `exportChromeTrace(path)` — open in `chrome://tracing`, Perfetto, or speedscope.
- `stitchByCorr(corr)` → `vector<WaterfallEntry>` (or `printWaterfall(corr, out)`) — per-request waterfall across threads.
- `writeTape(path)` / `readTape(path)` — versioned raw dump.

## Consuming rune-fr

Add to your `conandata.yml`:
```yaml
requirements:
  - "rune-fr/0.1.0-snapshot"
```
Then in CMake:
```cmake
find_package(rune_fr CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE rune_fr::rune_fr)
```
Toggle recording off at build time with `-o rune-fr/*:rune_fr=False` (compiles to nothing).

A minimal end-to-end consumer lives in [`sample/`](sample/): it links `rune_fr::rune_fr`,
records a few spans, prints histograms, and writes a Chrome trace.

## Overhead

Measured with Google Benchmark, Release `-O3`, `--benchmark_min_time=1s`.

Dev box (macOS, Apple Silicon arm64, `cntvct_el0` timestamp path):

| case | ns/op |
|------|-------|
| enabled SPAN (`begin()`+`commit()`) | **8.56** |
| enabled MARK (instant) | **8.54** |
| runtime-disabled SPAN (compiled-in) | **0.66** |
| compiled-out (`RUNE_FR=0`) | 0 |

Both spec targets are met: enabled ≤ 10 ns, disabled ≤ 1 ns. The enabled cost is the
per-thread M2O ring's uncontended claim → 24-byte copy → commit (the framing that lets a
drainer read a live ring without ever tearing a record). The x86_64 reference VM uses the
`rdtscp` timestamp path; confirm its numbers on that VM in CI. Reproduce:

```bash
cmake --preset conan-release -DBUILD_BENCHMARKS=ON
cmake --build --preset conan-release --target rune_fr_bench
./build/build/Release/rune_fr_bench --benchmark_min_time=1s
```
