#include "flight_recorder/Calibration.h"
#include "flight_recorder/Tsc.h"

#include <atomic>
#include <ctime>

namespace flight_recorder {
namespace {
std::atomic<double> g_tscPerNs{1.0};
std::atomic<bool>   g_calibrated{false};

uint64_t monotonicRawNs() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}
} // namespace

void calibrate() noexcept {
    bool expected = false;
    if (!g_calibrated.compare_exchange_strong(expected, true)) return;

#if defined(FLIGHT_RECORDER_FALLBACK)
    g_tscPerNs.store(1.0, std::memory_order_relaxed); // rdtscp() already returns ns
    return;
#elif defined(FLIGHT_RECORDER_ARM)
    uint64_t f = counterFrequencyHz();
    g_tscPerNs.store(f > 0 ? static_cast<double>(f) / 1e9 : 1.0,
                     std::memory_order_relaxed);
    return;
#else
    // x86: measure TSC ticks over a fixed CLOCK_MONOTONIC_RAW window.
    constexpr uint64_t kWindowNs = 10'000'000ull; // 10 ms
    uint64_t nsStart = monotonicRawNs();
    uint64_t tscStart = rdtscp();
    uint64_t nsNow;
    do {
        nsNow = monotonicRawNs();
    } while (nsNow - nsStart < kWindowNs);
    uint64_t tscEnd = rdtscp();

    double elapsedNs = static_cast<double>(nsNow - nsStart);
    double elapsedTsc = static_cast<double>(tscEnd - tscStart);
    double perNs = elapsedNs > 0.0 ? elapsedTsc / elapsedNs : 1.0;
    g_tscPerNs.store(perNs > 0.0 ? perNs : 1.0, std::memory_order_relaxed);
#endif
}

double tscPerNs() noexcept {
    return g_tscPerNs.load(std::memory_order_relaxed);
}

} // namespace flight_recorder
