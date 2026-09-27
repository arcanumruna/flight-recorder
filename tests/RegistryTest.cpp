#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <set>
#include "rune_fr/Recorder.h"

using namespace rune_fr;

TEST(Registry, SameThreadReturnsSameRecorder) {
    Recorder::instance().reset();
    ThreadRecorder* a = threadRecorder();
    ThreadRecorder* b = threadRecorder();
    EXPECT_EQ(a, b);
    EXPECT_NE(a, nullptr);
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
