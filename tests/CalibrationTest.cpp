#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "rune_fr/Tsc.h"
#include "rune_fr/Calibration.h"

TEST(Calibration, TscPerNsIsPositive) {
    rune_fr::calibrate();
    EXPECT_GT(rune_fr::tscPerNs(), 0.0);
}

TEST(Calibration, RdtscpMonotonicWithinThread) {
    uint64_t a = rune_fr::rdtscp();
    uint64_t b = rune_fr::rdtscp();
    EXPECT_GE(b, a);
}

TEST(Calibration, KnownSleepMeasuresWithinTolerance) {
    rune_fr::calibrate();
    uint64_t start = rune_fr::rdtscp();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint64_t end = rune_fr::rdtscp();
    double measuredNs = rune_fr::cyclesToNs(end - start);
    // 50 ms = 50e6 ns; allow generous scheduler slop (30%..300%).
    EXPECT_GT(measuredNs, 35'000'000.0);
    EXPECT_LT(measuredNs, 150'000'000.0);
}

TEST(Calibration, FallbackNeverCrashesAndStaysPositive) {
    // Whatever platform this runs on, calibrate + read must be safe and > 0.
    rune_fr::calibrate();
    EXPECT_TRUE(rune_fr::tscPerNs() > 0.0);
    (void)rune_fr::hasInvariantTsc(); // must not crash on any target
}
