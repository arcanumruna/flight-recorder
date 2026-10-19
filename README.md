# flight-recorder

Ultra-low-overhead C++23 flight recorder (JFR-style begin/commit spans) for Rune's hot paths.

An event is *measured*: `begin()` stamps a start TSC, `commit()` stamps the end and appends a
completed 24-byte record to a per-thread, lock-free, zero-allocation ring buffer. A separate
off-hot-path drainer turns raw tapes into per-site latency histograms and a Chrome-trace
timeline, so you can always answer "what took long?" without ad-hoc A/B benchmarking.

## Build

```bash
conan install . -of build -s build_type=Release -s compiler.cppstd=23 -o flight-recorder/*:flight_recorder=True --build=missing
cmake --preset conan-release -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON
cmake --build --preset conan-release -j
ctest --preset conan-release --output-on-failure
```

(For a debug/test cycle, substitute `build_type=Debug` and the `conan-debug` preset.)

### Dependency: aeron (and macOS/armv8)

The per-thread ring (`flight_recorder::detail::M2ORingBuffer`) is a namespace-isolated copy of Rune's
agrona M2O ring built on aeron's **header-only** `AtomicBuffer`/`RecordDescriptor` — flight-recorder
depends on **aeron**, not on `common`. The aeron ConanCenter recipe rejects macOS/armv8, so
there aeron is skipped (see `conanfile.py`) and CMake provides a stub `aeron::aeron` target so
configure/IDE loading works. To actually build/test on macOS, point at cached aeron headers:

```bash
cmake --preset conan-debug -DBUILD_TESTING=ON \
  -DFLIGHT_RECORDER_AERON_INCLUDE_DIR=$(dirname $(dirname $(find ~/.conan2 -path '*p/include/concurrent/AtomicBuffer.h' | head -1)))
```

The ring is **drop-on-full**: `commit()` never blocks the hot path; if a thread's ring is full
(drainer not keeping up), the event is dropped rather than overwriting or stalling.

## Usage

```cpp
#include <flight_recorder/FlightRecorder.h>

flight_recorder::Event ev(flight_recorder::Site::WorkerAppend);
ev.begin().corr(correlationId);
// ... work ...
ev.commit();

// or RAII:
FLIGHT_RECORDER_SPAN(flight_recorder::Site::WorkerAppend, correlationId);
```

## Reports

Off the hot path, drain via `flight_recorder::Recorder`:
- `dumpHistograms(out)` — per-site p50/p90/p99/p999/max/count/mean (ns).
- `exportChromeTrace(path)` — open in `chrome://tracing`, Perfetto, or speedscope.
- `stitchByCorr(corr)` → `vector<WaterfallEntry>` (or `printWaterfall(corr, out)`) — per-request waterfall across threads.
- `writeTape(path)` / `readTape(path)` — versioned raw dump.

## Consuming flight-recorder

Add to your `conandata.yml`:
```yaml
requirements:
  - "flight-recorder/0.1.0-snapshot"
```
Then in CMake:
```cmake
find_package(flight_recorder CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE flight_recorder::flight_recorder)
```
Toggle recording off at build time with `-o flight-recorder/*:flight_recorder=False` (compiles to nothing).

A minimal end-to-end consumer lives in [`sample/`](sample/): it links `flight_recorder::flight_recorder`,
records a few spans, prints histograms, and writes a Chrome trace.

## Overhead

Measured with Google Benchmark, Release `-O3`, `--benchmark_min_time=1s`.

Dev box (macOS, Apple Silicon arm64, `cntvct_el0` timestamp path):

| case | ns/op |
|------|-------|
| enabled SPAN (`begin()`+`commit()`) | **8.56** |
| enabled MARK (instant) | **8.54** |
| runtime-disabled SPAN (compiled-in) | **0.66** |
| compiled-out (`FLIGHT_RECORDER=0`) | 0 |

Both spec targets are met: enabled ≤ 10 ns, disabled ≤ 1 ns. The enabled cost is the
per-thread M2O ring's uncontended claim → 24-byte copy → commit (the framing that lets a
drainer read a live ring without ever tearing a record). The x86_64 reference VM uses the
`rdtscp` timestamp path; confirm its numbers on that VM in CI. Reproduce:

```bash
cmake --preset conan-release -DBUILD_BENCHMARKS=ON
cmake --build --preset conan-release --target flight_recorder_bench
./build/build/Release/flight_recorder_bench --benchmark_min_time=1s
```
