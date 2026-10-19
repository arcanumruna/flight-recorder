#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include "flight_recorder/FlightRecorder.h"

using namespace flight_recorder;

namespace {
void pushSynthetic(Site s, uint32_t corr, uint64_t start, uint64_t end) {
    threadRecorder()->ring.push(
        FrRecord{start, end, corr, static_cast<uint16_t>(s), 0});
}
nlohmann::json loadJson(const std::string& path) {
    std::ifstream in(path);
    nlohmann::json j;
    in >> j;
    return j;
}
} // namespace

TEST(ChromeTrace, EmptyProducesOnlyTheAnchor) {
    Recorder::instance().reset();
    const std::string path = "/tmp/flight_recorder_empty_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));
    auto j = loadJson(path);
    EXPECT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 1u);
    EXPECT_EQ(j[0].at("name"), "flight_recorder_anchor");
    EXPECT_EQ(j[0].at("args").at("anchorTsc"), 0);
    std::remove(path.c_str());
}

TEST(ChromeTrace, AnchorCarriesTheAlignmentSample) {
    Recorder::instance().reset();
    calibrate();
    pushSynthetic(Site::WorkerAppend, 1, 7000, 9000);
    pushSynthetic(Site::WorkerAppend, 2, 5000, 6000);
    const std::string path = "/tmp/flight_recorder_anchor_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));
    auto j = loadJson(path);
    ASSERT_EQ(j.size(), 3u);
    const auto& a = j[0];
    EXPECT_EQ(a.at("ph"), "M");
    EXPECT_EQ(a.at("name"), "flight_recorder_anchor");
    EXPECT_EQ(a.at("args").at("anchorTsc"), 5000);   // earliest startTsc
    EXPECT_GT(a.at("args").at("tscPerNs").get<double>(), 0.0);
    EXPECT_GT(a.at("args").at("syncTsc").get<uint64_t>(), 0u);
    EXPECT_GT(a.at("args").at("syncRealNs").get<uint64_t>(), 0u);
    EXPECT_TRUE(a.at("args").contains("host"));
    EXPECT_DOUBLE_EQ(j[2].at("ts").get<double>(), 0.0);   // relative to the anchor
    std::remove(path.c_str());
}

TEST(ChromeTrace, EmitsCompleteEventsWithRequiredFields) {
    Recorder::instance().reset();
    calibrate();
    pushSynthetic(Site::WorkerAppend, 42, 1000, 5000);
    const std::string path = "/tmp/flight_recorder_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));

    auto j = loadJson(path);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 2u);   // anchor metadata + the event
    const auto& e = j[1];
    EXPECT_EQ(e.at("ph"), "X");
    EXPECT_EQ(e.at("name"), "WorkerAppend");
    EXPECT_TRUE(e.contains("ts"));
    EXPECT_TRUE(e.contains("dur"));
    EXPECT_EQ(e.at("pid"), 1);
    EXPECT_TRUE(e.contains("tid"));
    EXPECT_EQ(e.at("args").at("corr"), 42);
    std::remove(path.c_str());
}
