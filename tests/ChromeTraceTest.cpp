#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include "rune_fr/FlightRecorder.h"

using namespace rune_fr;

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

TEST(ChromeTrace, EmptyProducesValidEmptyArray) {
    Recorder::instance().reset();
    const std::string path = "/tmp/rune_fr_empty_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));
    auto j = loadJson(path);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 0u);
    std::remove(path.c_str());
}

TEST(ChromeTrace, EmitsCompleteEventsWithRequiredFields) {
    Recorder::instance().reset();
    calibrate();
    pushSynthetic(Site::WorkerAppend, 42, 1000, 5000);
    const std::string path = "/tmp/rune_fr_trace.json";
    ASSERT_TRUE(exportChromeTrace(path));

    auto j = loadJson(path);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 1u);
    const auto& e = j[0];
    EXPECT_EQ(e.at("ph"), "X");
    EXPECT_EQ(e.at("name"), "WorkerAppend");
    EXPECT_TRUE(e.contains("ts"));
    EXPECT_TRUE(e.contains("dur"));
    EXPECT_EQ(e.at("pid"), 1);
    EXPECT_TRUE(e.contains("tid"));
    EXPECT_EQ(e.at("args").at("corr"), 42);
    std::remove(path.c_str());
}
