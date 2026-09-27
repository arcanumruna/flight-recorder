#include <gtest/gtest.h>
#include <vector>
#include "rune_fr/FlightRecorder.h"

using namespace rune_fr;

namespace {
size_t liveRecords() {
    size_t total = 0;
    std::vector<FrRecord> tmp;
    for (auto* tr : Recorder::instance().snapshotThreads()) {
        total += tr->ring.snapshot(tmp);
    }
    return total;
}
} // namespace

TEST(Event, BeginCommitProducesOneCorrectRecord) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(categoryBit(Category::Produce));

    Event ev(Site::WorkerAppend);
    ev.begin().corr(4242).arg(7);
    ev.commit();

    auto threads = Recorder::instance().snapshotThreads();
    ASSERT_EQ(threads.size(), 1u);
    std::vector<FrRecord> out;
    ASSERT_EQ(threads[0]->ring.snapshot(out), 1u);
    EXPECT_EQ(out[0].site, static_cast<uint16_t>(Site::WorkerAppend));
    EXPECT_EQ(out[0].corr, 4242u);
    EXPECT_EQ(out[0].arg, 7u);
    EXPECT_GE(out[0].endTsc, out[0].startTsc); // end >= start
    disable();
}

TEST(Event, DisabledProducesNoRecord) {
    Recorder::instance().reset();
    disable();
    Event ev(Site::WorkerAppend);
    ev.begin().corr(1);
    ev.commit();
    EXPECT_EQ(liveRecords(), 0u);
}

TEST(Event, CategoryMaskedOutProducesNoRecord) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(categoryBit(Category::Storage)); // WorkerAppend is Produce
    Event ev(Site::WorkerAppend);
    ev.begin();
    ev.commit();
    EXPECT_EQ(liveRecords(), 0u);
    disable();
}

TEST(Event, ScopedSpanRecordsOnDestruction) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    {
        RUNE_FR_SPAN(Site::EgressSend, 99);
    }
    EXPECT_EQ(liveRecords(), 1u);
    disable();
}

TEST(Event, MarkRecordsInstantWithEqualStartEnd) {
    // Review Focus: mark => startTsc == endTsc, duration 0, accepted.
    Recorder::instance().reset();
    enable();
    setCategoryMask(0xFFFFFFFFu);
    RUNE_FR_MARK(Site::IngressRecv, 5);
    auto threads = Recorder::instance().snapshotThreads();
    ASSERT_EQ(threads.size(), 1u);
    std::vector<FrRecord> out;
    ASSERT_EQ(threads[0]->ring.snapshot(out), 1u);
    EXPECT_EQ(out[0].startTsc, out[0].endTsc);
    EXPECT_EQ(out[0].durationTsc(), 0u);
    disable();
}
