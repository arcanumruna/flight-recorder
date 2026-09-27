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
- `stitchByCorr(corr, out)` — per-request waterfall across threads.
- `writeTape(path)` / `readTape(path)` — versioned raw dump.

## Overhead

Measured with Google Benchmark, Release `-O3`, `--benchmark_min_time=1s`.

Dev box (macOS, Apple Silicon arm64, `cntvct_el0` timestamp path):

| case | ns/op |
|------|-------|
| enabled SPAN (`begin()`+`commit()`) | **2.91** |
| enabled MARK (instant) | **2.94** |
| runtime-disabled SPAN (compiled-in) | **0.66** |
| compiled-out (`RUNE_FR=0`) | 0 |

Both targets from the spec are met with wide margin: enabled ≤ 10 ns, disabled ≤ 1 ns.
The x86_64 reference VM uses the `rdtscp` path (a single serializing counter read + one
store on `commit()`); its numbers are expected to be in the same few-ns range and should be
confirmed by CI on that VM. Reproduce:

```bash
cmake --preset conan-release -DBUILD_BENCHMARKS=ON
cmake --build --preset conan-release --target rune_fr_bench
./build/build/Release/rune_fr_bench --benchmark_min_time=1s
```
