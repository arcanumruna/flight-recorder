#include <gtest/gtest.h>
#include <sstream>
#include "rune_fr/FlightRecorder.h"

using namespace rune_fr;

namespace {
// Push a record directly into this thread's ring, bypassing the TSC clock so
// durations are deterministic (in "cycles" == ns here since we control values).
void pushSynthetic(Site s, uint32_t corr, uint64_t start, uint64_t end, uint16_t arg = 0) {
    threadRecorder()->ring.push(
        FrRecord{start, end, corr, static_cast<uint16_t>(s), arg});
}
} // namespace

TEST(ReportHistogram, EmptyRingsProduceNoStatsAndDontCrash) {
    Recorder::instance().reset();
    std::ostringstream os;
    dumpHistograms(os); // must not throw / divide by zero
    EXPECT_TRUE(collectSiteStats().empty());
}

TEST(ReportHistogram, AggregatesDurationsPerSite) {
    Recorder::instance().reset();
    calibrate();
    for (uint64_t d = 100; d <= 1000; d += 100)
        pushSynthetic(Site::WorkerAppend, 1, 0, d);

    auto stats = collectSiteStats();
    ASSERT_FALSE(stats.empty());
    bool found = false;
    for (auto& s : stats) {
        if (s.site == Site::WorkerAppend) {
            found = true;
            EXPECT_EQ(s.count, 10u);
            EXPECT_GT(s.max, 0u);
            EXPECT_GT(s.p50, 0u);
        }
    }
    EXPECT_TRUE(found);
}

TEST(Stitch, CorrZeroReturnsEmpty) {
    // Review Focus: corr==0 is the "none" sentinel; never stitch it.
    Recorder::instance().reset();
    pushSynthetic(Site::IngressRecv, 0, 10, 20);
    pushSynthetic(Site::WorkerAppend, 0, 30, 40);
    EXPECT_TRUE(stitchByCorr(0).empty());
}

TEST(Stitch, OrdersByStartTsc) {
    Recorder::instance().reset();
    pushSynthetic(Site::EgressSend, 77, 300, 350);
    pushSynthetic(Site::IngressRecv, 77, 100, 150);
    pushSynthetic(Site::WorkerAppend, 77, 200, 260);

    auto wf = stitchByCorr(77);
    ASSERT_EQ(wf.size(), 3u);
    EXPECT_EQ(wf[0].site, Site::IngressRecv);
    EXPECT_EQ(wf[1].site, Site::WorkerAppend);
    EXPECT_EQ(wf[2].site, Site::EgressSend);
    EXPECT_EQ(wf[0].offsetNs, 0u); // earliest is the anchor
}
