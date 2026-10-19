#include <benchmark/benchmark.h>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

// Enabled SPAN: begin+commit of a real measured event.
static void BM_SpanEnabled(benchmark::State& state) {
    Recorder::instance().reset();
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    for (auto _ : state) {
        Event ev(Site::WorkerAppend);
        ev.begin().corr(1);
        ev.commit();
    }
    disable();
}
BENCHMARK(BM_SpanEnabled)->Unit(benchmark::kNanosecond);

// Runtime-disabled (compiled-in): should be ~1 ns (one predicted branch path).
static void BM_SpanDisabled(benchmark::State& state) {
    disable();
    for (auto _ : state) {
        Event ev(Site::WorkerAppend);
        ev.begin().corr(1);
        ev.commit();
        benchmark::DoNotOptimize(ev);
    }
}
BENCHMARK(BM_SpanDisabled)->Unit(benchmark::kNanosecond);

// Enabled MARK: single-timestamp instant.
static void BM_MarkEnabled(benchmark::State& state) {
    Recorder::instance().reset();
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    for (auto _ : state) {
        FLIGHT_RECORDER_MARK(Site::IngressRecv, 1);
    }
    disable();
}
BENCHMARK(BM_MarkEnabled)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
