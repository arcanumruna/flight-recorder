#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

TEST(Throughput, MultiThreadNoInterferenceEachRingHoldsItsOwn) {
    Recorder::instance().reset();
    enable();
    setCategoryMask(0xFFFFFFFFu);

    constexpr int kThreads = 4;
    // A deterministic count well under the default ring capacity so we can
    // assert exact retention (no overwrite) and thus per-thread isolation.
    constexpr uint64_t kUnderCap = 1000;

    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&] {
            for (uint64_t n = 0; n < kUnderCap; ++n) {
                Event ev(Site::WorkerAppend);
                ev.begin().corr(static_cast<uint32_t>(n));
                ev.commit();
            }
        });
    }
    for (auto& t : ts) t.join();

    Recorder::instance().drain();
    auto threads = Recorder::instance().snapshotThreads();
    EXPECT_EQ(threads.size(), static_cast<size_t>(kThreads));
    for (auto* tr : threads) {
        EXPECT_EQ(tr->drained.size(), kUnderCap); // each thread kept exactly its own
    }
    disable();
}
