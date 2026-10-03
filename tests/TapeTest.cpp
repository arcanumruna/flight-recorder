#include <gtest/gtest.h>
#include <fstream>
#include <atomic>
#include <cstdio>
#include <thread>
#include "rune_fr/FlightRecorder.h"

using namespace rune_fr;

TEST(Tape, RoundTripPreservesRecords) {
    Recorder::instance().reset();
    calibrate();
    threadRecorder()->ring.push(FrRecord{100, 250, 7, static_cast<uint16_t>(Site::WorkerAppend), 3});
    threadRecorder()->ring.push(FrRecord{300, 400, 8, static_cast<uint16_t>(Site::EgressSend), 0});

    const std::string path = "/tmp/rune_fr_test.tape";
    ASSERT_TRUE(writeTape(path));

    Tape t;
    ASSERT_TRUE(readTape(path, t));
    EXPECT_EQ(t.version, 1u);
    EXPECT_GT(t.tscPerNs, 0.0);
    EXPECT_EQ(t.siteNames.size(), static_cast<size_t>(Site::Count));
    ASSERT_EQ(t.threads.size(), 1u);
    ASSERT_EQ(t.threads[0].records.size(), 2u);
    EXPECT_EQ(t.threads[0].records[0].corr, 7u);
    EXPECT_EQ(t.threads[0].records[0].site, static_cast<uint16_t>(Site::WorkerAppend));
    EXPECT_EQ(t.threads[0].records[0].arg, 3u);
    EXPECT_EQ(t.threads[0].records[1].corr, 8u);
    std::remove(path.c_str());
}

TEST(Tape, RejectsBadMagic) {
    const std::string path = "/tmp/rune_fr_bad.tape";
    { std::ofstream out(path); out << "NOTATAPE"; }
    Tape t;
    EXPECT_FALSE(readTape(path, t));
    std::remove(path.c_str());
}

TEST(Tape, EmptyRingsWriteReadableTape) {
    Recorder::instance().reset();
    const std::string path = "/tmp/rune_fr_empty.tape";
    ASSERT_TRUE(writeTape(path));
    Tape t;
    ASSERT_TRUE(readTape(path, t));
    EXPECT_EQ(t.threads.size(), 0u);
    std::remove(path.c_str());
}

// D1 regression: every export used to call drain() and then walk each thread's `drained` tape WITHOUT the
// registry lock, while the background drainer appended to / trimmed the same vectors (realloc / erase under
// the reader) — the broker segfaulted exporting at shutdown.  Exports must read the tapes under the lock.
// Deterministic under TSan (data race); under a plain build it crashes or reads garbage intermittently.
TEST(TapeRegression, ExportWhileBackgroundDrainingIsRaceFree) {
    Recorder::instance().reset();
    calibrate();
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        uint64_t i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            threadRecorder()->ring.push(FrRecord{i, i + 10, static_cast<uint32_t>(i), static_cast<uint16_t>(Site::WorkerAppend), 0});
            ++i;
        }
    });
    startBackgroundDrain(1);
    const std::string trace = "/tmp/rune_fr_race_trace.json", tape = "/tmp/rune_fr_race.tape";
    for (int k = 0; k < 20; ++k) {
        EXPECT_TRUE(exportChromeTrace(trace));
        EXPECT_TRUE(writeTape(tape));
    }
    stopBackgroundDrain();
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    std::remove(trace.c_str());
    std::remove(tape.c_str());
    Recorder::instance().reset();
}
