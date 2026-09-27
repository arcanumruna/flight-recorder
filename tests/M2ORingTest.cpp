#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "rune_fr/detail/FrRing.h"

using rune_fr::FrRecord;
using rune_fr::detail::FrRing;

TEST(M2ORing, PushDrainRoundTripPreservesOrderAndContent) {
    FrRing ring(4096);
    for (uint64_t i = 1; i <= 5; ++i) {
        EXPECT_TRUE(ring.push(FrRecord{i, i + 10, static_cast<uint32_t>(i),
                                       static_cast<uint16_t>(i), static_cast<uint16_t>(i * 2)}));
    }
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.drainInto(out), 5u);
    ASSERT_EQ(out.size(), 5u);
    for (uint64_t i = 1; i <= 5; ++i) {
        const auto& r = out[i - 1];
        EXPECT_EQ(r.startTsc, i);
        EXPECT_EQ(r.endTsc, i + 10);
        EXPECT_EQ(r.corr, static_cast<uint32_t>(i));
        EXPECT_EQ(r.arg, static_cast<uint16_t>(i * 2));
    }
}

TEST(M2ORing, DrainEmptyYieldsNothing) {
    FrRing ring(4096);
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.drainInto(out), 0u);
    EXPECT_TRUE(out.empty());
}

TEST(M2ORing, DropsWhenFull) {
    // Tiny ring: 256 payload bytes; each record frames to 32 bytes -> ~8 fit.
    FrRing ring(256);
    std::size_t pushed = 0;
    for (uint64_t i = 0; i < 1000; ++i) {
        if (ring.push(FrRecord{i, i, static_cast<uint32_t>(i), 1, 0})) ++pushed;
    }
    EXPECT_GT(pushed, 0u);
    EXPECT_LT(pushed, 1000u); // dropped once full (never blocks)
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.drainInto(out), pushed); // exactly what was accepted
}

TEST(M2ORing, UndersizedCapacityClampsInsteadOfCrashing) {
    // A payload capacity below kMinPayloadCapacity would make maxMsgLength < 24,
    // so tryClaim(1, sizeof(FrRecord)) would throw inside the noexcept push() and
    // std::terminate. FrRing must clamp it up so push() only ever drops, never
    // crashes.
    FrRing ring(64); // clamped up to kMinPayloadCapacity
    std::size_t pushed = 0;
    for (uint64_t i = 0; i < 100; ++i) {
        if (ring.push(FrRecord{i, i, static_cast<uint32_t>(i), 1, 0})) ++pushed;
    }
    EXPECT_GT(pushed, 0u); // at least one record fits (no crash, no throw)
    std::vector<FrRecord> out;
    EXPECT_EQ(ring.drainInto(out), pushed);
}

TEST(M2ORing, NoTornRecordUnderConcurrentDrain) {
    // Producer pushes self-consistent records (startTsc==endTsc,
    // corr==(uint32_t)startTsc) while a drainer reads concurrently. M2O framing
    // guarantees the consumer never sees a partially-written record.
    FrRing ring(4096);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> torn{0}, checked{0};

    std::thread producer([&] {
        for (uint64_t n = 1; !stop.load(std::memory_order_relaxed); ++n) {
            ring.push(FrRecord{n, n, static_cast<uint32_t>(n), 1, 0});
        }
    });

    std::vector<FrRecord> out;
    for (int iter = 0; iter < 200000; ++iter) {
        out.clear();
        ring.drainInto(out);
        for (const auto& r : out) {
            if (!(r.startTsc == r.endTsc && r.corr == static_cast<uint32_t>(r.startTsc)))
                torn.fetch_add(1, std::memory_order_relaxed);
            checked.fetch_add(1, std::memory_order_relaxed);
        }
    }
    stop.store(true, std::memory_order_relaxed);
    producer.join();

    EXPECT_EQ(torn.load(), 0u)
        << torn.load() << " torn of " << checked.load() << " checked";
    EXPECT_GT(checked.load(), 0u); // drainer actually observed records
}
