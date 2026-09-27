#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
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
