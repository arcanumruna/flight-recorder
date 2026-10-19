#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "flight_recorder/Tsc.h"
#include "flight_recorder/Calibration.h"

TEST(Calibration, TscPerNsIsPositive) {
    flight_recorder::calibrate();
    EXPECT_GT(flight_recorder::tscPerNs(), 0.0);
}

TEST(Calibration, RdtscpMonotonicWithinThread) {
    uint64_t a = flight_recorder::rdtscp();
    uint64_t b = flight_recorder::rdtscp();
    EXPECT_GE(b, a);
}

TEST(Calibration, KnownSleepMeasuresWithinTolerance) {
    flight_recorder::calibrate();
    uint64_t start = flight_recorder::rdtscp();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint64_t end = flight_recorder::rdtscp();
    double measuredNs = flight_recorder::cyclesToNs(end - start);
    // 50 ms = 50e6 ns; allow generous scheduler slop (30%..300%).
    EXPECT_GT(measuredNs, 35'000'000.0);
    EXPECT_LT(measuredNs, 150'000'000.0);
}

TEST(Calibration, FallbackNeverCrashesAndStaysPositive) {
    // Whatever platform this runs on, calibrate + read must be safe and > 0.
    flight_recorder::calibrate();
    EXPECT_TRUE(flight_recorder::tscPerNs() > 0.0);
    (void)flight_recorder::hasInvariantTsc(); // must not crash on any target
}
