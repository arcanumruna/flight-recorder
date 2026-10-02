// Hop instrumentation primitives: correlation keys, SlowSpan, Throttle,
// markArgVal, drop accounting, the background drainer and the run dump (meta
// with the cross-process anchor).
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

#include "rune_fr/FlightRecorder.h"
#include "rune_fr/capi.h"

using namespace rune_fr;

namespace {
std::vector<FrRecord> drainedRecords() {
    Recorder::instance().drain();
    std::vector<FrRecord> out;
    for (auto* tr : Recorder::instance().snapshotThreads())
        out.insert(out.end(), tr->drained.begin(), tr->drained.end());
    return out;
}
void armAll() {
    Recorder::instance().reset();
    calibrate();
    setCategoryMask(0xFFFFFFFFu);
    setSlowSpanTsc(0);
    setThrottleTsc(0);
    enable();
}
} // namespace

TEST(Keys, FoldCorrKeepsSlotByteAndSequenceDropsAttempt) {
    // Rune client batch id: tag | ordinal<<50 | slot<<32 | attempt<<24 | seq.
    const int64_t base = (int64_t{1} << 62) | (int64_t{3} << 50) | (int64_t{5} << 32) | 0x123456;
    const int64_t retry = base | (int64_t{7} << 24);   // attempt 7 of the same batch
    EXPECT_EQ(foldCorr(base), (5u << 24) | 0x123456u);
    EXPECT_EQ(foldCorr(retry), foldCorr(base));
    const int64_t otherPartition = (base & ~(int64_t{0x3FFFF} << 32)) | (int64_t{6} << 32);
    EXPECT_NE(foldCorr(otherPartition), foldCorr(base));
    EXPECT_EQ(foldCorr(42), 42u);   // a small plain id folds to itself
}

TEST(Keys, PartKeyPacksTopicPartitionAndOffset24) {
    EXPECT_EQ(partKey(0, 3, 0x1000005), (8u << 28) | (3u << 24) | 0x000005u);
    EXPECT_EQ(partKey(-1, 0, 7) >> 28, 7u);   // internal topic keeps its own nibble
    static_assert(partKey(1, 2, 3) == ((9u << 28) | (2u << 24) | 3u));
}

TEST(HopPrimitives, MarkArgValCarriesArgAndValue) {
    armAll();
    markArgVal(Site::ShIngressFrame, 99, 3, 1024.0);
    auto r = drainedRecords();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].site, static_cast<uint16_t>(Site::ShIngressFrame));
    EXPECT_EQ(r[0].corr, 99u);
    EXPECT_EQ(r[0].arg, 3u);
    EXPECT_DOUBLE_EQ(r[0].value, 1024.0);
    EXPECT_EQ(r[0].startTsc, r[0].endTsc);
    disable();
}

TEST(HopPrimitives, SlowSpanKeepsOnlySpansAboveTheThreshold) {
    armAll();
    setSlowSpanTsc(nsToCycles(5'000'000.0));   // 5 ms
    { SlowSpan fast(Site::ShardPass, 1); }
    EXPECT_EQ(drainedRecords().size(), 0u);
    {
        SlowSpan slow(Site::ShardPass, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    auto r = drainedRecords();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].corr, 2u);
    EXPECT_GE(r[0].durationTsc(), nsToCycles(5'000'000.0));
    setSlowSpanTsc(0);
    { SlowSpan any(Site::ShardPass, 3); }   // threshold 0: every span kept
    EXPECT_EQ(drainedRecords().size(), 2u);
    disable();
}

TEST(HopPrimitives, ThrottleCoalescesARepeatingConditionAndCountsIt) {
    armAll();
    setThrottleTsc(nsToCycles(50'000'000.0));   // 50 ms: everything below is one burst
    Throttle thr;
    for (int i = 0; i < 1000; ++i) thr.hit(Site::ShPushRefused, 7, 2);
    auto r = drainedRecords();
    ASSERT_EQ(r.size(), 1u);                 // the first event is emitted at once
    EXPECT_DOUBLE_EQ(r[0].value, 1.0);
    EXPECT_EQ(r[0].arg, 2u);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    thr.hit(Site::ShPushRefused, 7, 2);      // past the gap: carries the 999 coalesced + itself
    r = drainedRecords();
    ASSERT_EQ(r.size(), 2u);
    EXPECT_DOUBLE_EQ(r[1].value, 1000.0);
    disable();
}

TEST(HopPrimitives, DisabledThrottleAndSlowSpanRecordNothingAndKeepNoState) {
    armAll();
    disable();
    Throttle thr;
    for (int i = 0; i < 10; ++i) thr.hit(Site::ShPushRefused, 1, 0);
    { SlowSpan s(Site::ShardPass, 1); }
    EXPECT_EQ(thr.pending, 0u);
    EXPECT_EQ(thr.lastTsc, 0u);
    EXPECT_EQ(drainedRecords().size(), 0u);
}

TEST(HopPrimitives, FullRingDropsAreCounted) {
    Recorder::instance().reset();
    Recorder::instance().configure(256);   // the smallest ring: a handful of records
    calibrate();
    setCategoryMask(0xFFFFFFFFu);
    enable();
    const uint64_t before = droppedRecords();
    for (int i = 0; i < 100; ++i) mark(Site::ShFetchSent, static_cast<uint32_t>(i));
    EXPECT_GT(droppedRecords(), before);
    disable();
    Recorder::instance().configure(kDefaultRingCapacity);
    Recorder::instance().reset();
}

TEST(HopPrimitives, BackgroundDrainEmptiesTheRingsWhileRunning) {
    armAll();
    startBackgroundDrain(5);
    for (int i = 0; i < 100; ++i) mark(Site::ShFetchSent, static_cast<uint32_t>(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    // The drainer moved them into the tape without anyone calling drain().
    size_t drained = 0;
    for (auto* tr : Recorder::instance().snapshotThreads()) drained += tr->drained.size();
    stopBackgroundDrain();
    EXPECT_EQ(drained, 100u);
    disable();
}

TEST(HopPrimitives, DumpAllWritesTapeTraceHistAndMetaWithAnchor) {
    armAll();
    mark(Site::ClientBatchOffer, 5);
    markVal(Site::ClientAckRecv, 5, 0.0);
    const std::string dir = "/tmp";
    const std::string sfx = "-test-p" + std::to_string(::getpid());
    ASSERT_TRUE(dumpAll(dir, sfx, "client", "c0"));
    std::ifstream m(dir + "/fr-meta" + sfx + ".json");
    ASSERT_TRUE(m.good());
    nlohmann::json j;
    m >> j;
    EXPECT_EQ(j.at("role"), "client");
    EXPECT_EQ(j.at("node"), "c0");
    EXPECT_EQ(j.at("pid").get<long>(), static_cast<long>(::getpid()));
    EXPECT_EQ(j.at("records").get<uint64_t>(), 2u);
    EXPECT_GT(j.at("anchorTsc").get<uint64_t>(), 0u);
    EXPECT_GE(j.at("syncTsc").get<uint64_t>(), j.at("anchorTsc").get<uint64_t>());
    EXPECT_GT(j.at("tscPerNs").get<double>(), 0.0);
    Tape t;
    ASSERT_TRUE(readTape(dir + "/fr-tape" + sfx + ".bin", t));
    size_t n = 0;
    for (const auto& th : t.threads) n += th.records.size();
    EXPECT_EQ(n, 2u);
    for (const char* f : {"/fr-meta", "/fr-tape", "/fr-trace", "/fr-hist"}) {
        const std::string ext = std::string(f) == "/fr-tape" ? ".bin" : std::string(f) == "/fr-hist" ? ".txt" : ".json";
        std::remove((dir + f + sfx + ext).c_str());
    }
    disable();
    EXPECT_FALSE(dumpAll(dir, sfx, "client", "c0"));   // off: nothing written
}

TEST(HopPrimitives, ConfigureFromEnvIsOffUnlessEnabledAndAppliesThresholds) {
    disable();
    ::unsetenv("RUNE_FR_ENABLE");
    EXPECT_FALSE(configureFromEnv());
    EXPECT_FALSE(isEnabled());
    ::setenv("RUNE_FR_ENABLE", "0", 1);
    EXPECT_FALSE(configureFromEnv());
    ::setenv("RUNE_FR_ENABLE", "1", 1);
    ::setenv("RUNE_FR_SLOW_SPAN_NS", "1000000", 1);
    ::setenv("RUNE_FR_THROTTLE_NS", "2000000", 1);
    ::setenv("RUNE_FR_CATEGORIES", "storage,produce", 1);
    ::setenv("RUNE_FR_DRAIN_MS", "0", 1);
    EXPECT_TRUE(configureFromEnv());
    EXPECT_TRUE(isEnabled());
    EXPECT_NEAR(cyclesToNs(slowSpanTsc()), 1'000'000.0, 1000.0);
    EXPECT_NEAR(cyclesToNs(throttleTsc()), 2'000'000.0, 2000.0);
    EXPECT_EQ(categoryMask(), categoryBit(Category::Storage) | categoryBit(Category::Produce));
    for (const char* v : {"RUNE_FR_ENABLE", "RUNE_FR_SLOW_SPAN_NS", "RUNE_FR_THROTTLE_NS", "RUNE_FR_CATEGORIES",
                          "RUNE_FR_DRAIN_MS"})
        ::unsetenv(v);
    disable();
    setCategoryMask(0xFFFFFFFFu);
    setSlowSpanTsc(0);
    setThrottleTsc(0);
}

TEST(HopPrimitives, CApiRecordsAeronSitesThroughTheSameRing) {
    armAll();
    const uint64_t t0 = rune_fr_c_now();
    rune_fr_c_span(RUNE_FR_C_AERON_SENDER_DUTY, 1, t0, 2.0);
    rune_fr_c_mark(RUNE_FR_C_AERON_NAK_RECV, 3, 4.0);
    rune_fr_c_mark(RUNE_FR_C_SITE_COUNT, 3, 4.0);   // out of range: ignored
    auto r = drainedRecords();
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].site, static_cast<uint16_t>(Site::AeronSenderDuty));
    EXPECT_EQ(r[1].site, static_cast<uint16_t>(Site::AeronNakRecv));
    disable();
}
