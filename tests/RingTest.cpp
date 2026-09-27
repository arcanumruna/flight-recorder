#include <gtest/gtest.h>
#include <vector>
#include "rune_fr/Ring.h"

using rune_fr::FrRecord;
using rune_fr::Ring;
using rune_fr::RingMode;

static FrRecord rec(uint64_t start) {
    return FrRecord{start, start + 1, 0, 1, 0};
}

TEST(Ring, FillBelowCapacityKeepsAll) {
    Ring ring(8, RingMode::OverwriteOldest);
    for (uint64_t i = 0; i < 5; ++i) ring.push(rec(i));
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.snapshot(out), 5u);
    EXPECT_EQ(out.front().startTsc, 0u);
    EXPECT_EQ(out.back().startTsc, 4u);
}

TEST(Ring, OverwriteOldestWrapsAndKeepsNewest) {
    Ring ring(4, RingMode::OverwriteOldest);
    for (uint64_t i = 0; i < 10; ++i) ring.push(rec(i)); // 6..9 survive
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.snapshot(out), 4u);
    EXPECT_EQ(out.front().startTsc, 6u);
    EXPECT_EQ(out.back().startTsc, 9u);
    EXPECT_EQ(ring.writeCount(), 10u);
}

TEST(Ring, StopWhenFullDropsNewest) {
    Ring ring(4, RingMode::StopWhenFull);
    for (uint64_t i = 0; i < 10; ++i) ring.push(rec(i)); // only 0..3 kept
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.snapshot(out), 4u);
    EXPECT_EQ(out.front().startTsc, 0u);
    EXPECT_EQ(out.back().startTsc, 3u);
}

TEST(Ring, RejectsNonPowerOfTwoCapacity) {
    // Review Focus: capacity must be a nonzero power of two.
    EXPECT_DEATH({ Ring bad(6, RingMode::OverwriteOldest); }, "power of two");
    EXPECT_DEATH({ Ring zero(0, RingMode::OverwriteOldest); }, "power of two");
}
