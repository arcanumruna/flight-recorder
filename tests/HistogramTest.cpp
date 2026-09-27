#include <gtest/gtest.h>
#include <cmath>
#include "rune_fr/Histogram.h"

using rune_fr::Histogram;

TEST(Histogram, EmptyIsSafe) {
    Histogram h;
    EXPECT_EQ(h.count(), 0u);
    EXPECT_EQ(h.valueAtPercentile(50.0), 0u);
    EXPECT_EQ(h.min(), 0u);
    EXPECT_EQ(h.mean(), 0.0);
}

TEST(Histogram, AcceptsZeroDurationSample) {
    // Review Focus: mark/instant events record duration 0.
    Histogram h;
    h.record(0);
    EXPECT_EQ(h.count(), 1u);
    EXPECT_EQ(h.min(), 0u);
    EXPECT_EQ(h.valueAtPercentile(50.0), 0u);
}

TEST(Histogram, UniformDistributionPercentilesWithinOnePercent) {
    Histogram h;
    for (uint64_t v = 1; v <= 100000; ++v) h.record(v);
    EXPECT_EQ(h.count(), 100000u);
    auto near = [](uint64_t got, double expected) {
        return std::fabs(static_cast<double>(got) - expected) / expected < 0.01;
    };
    EXPECT_TRUE(near(h.valueAtPercentile(50.0), 50000.0));
    EXPECT_TRUE(near(h.valueAtPercentile(90.0), 90000.0));
    EXPECT_TRUE(near(h.valueAtPercentile(99.0), 99000.0));
    EXPECT_TRUE(near(h.valueAtPercentile(99.9), 99900.0));
    EXPECT_GE(h.max(), 100000u * 99 / 100); // max bucket lower-bound near 100000
}

TEST(Histogram, IndexLowerBoundRoundTripBoundsError) {
    for (uint64_t v : {1ull, 255ull, 256ull, 300ull, 1000ull, 1ull<<20, 1ull<<40}) {
        uint64_t lb = Histogram::lowerBound(Histogram::index(v));
        EXPECT_LE(lb, v);
        if (v >= Histogram::kSub) {
            EXPECT_LT(static_cast<double>(v - lb) / v, 0.008); // < 0.8%
        }
    }
}
