#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
#include "flight_recorder/Recorder.h"

using namespace flight_recorder;

TEST(Registry, SameThreadReturnsSameRecorder) {
    Recorder::instance().reset();
    ThreadRecorder* a = threadRecorder();
    ThreadRecorder* b = threadRecorder();
    EXPECT_EQ(a, b);
    EXPECT_NE(a, nullptr);
}

TEST(Registry, ResetKeepsOldRecorderPointerValid) {
    // reset() must retire (not free) rings so a raw ThreadRecorder* obtained
    // before the reset stays valid memory (no use-after-free if a producer is
    // still pushing through it). A fresh threadRecorder() returns a new ring.
    Recorder::instance().reset();
    ThreadRecorder* before = threadRecorder();
    ASSERT_NE(before, nullptr);
    before->ring.push(FrRecord{1, 1, 1, 1, 0});

    Recorder::instance().reset();
    // Old pointer still points at live memory: reading and pushing must not crash.
    EXPECT_EQ(before->ring.push(FrRecord{2, 2, 2, 1, 0}), true);

    ThreadRecorder* after = threadRecorder();
    EXPECT_NE(after, before); // a fresh ring was registered after reset
}

TEST(Registry, EachThreadGetsItsOwnRing) {
    Recorder::instance().reset();
    constexpr int kThreads = 4;
    std::vector<ThreadRecorder*> seen(kThreads, nullptr);
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&, i] { seen[i] = threadRecorder(); });
    }
    for (auto& t : ts) t.join();

    std::set<ThreadRecorder*> unique(seen.begin(), seen.end());
    EXPECT_EQ(unique.size(), static_cast<size_t>(kThreads)); // all distinct rings
    EXPECT_EQ(Recorder::instance().snapshotThreads().size(),
              static_cast<size_t>(kThreads));
}
