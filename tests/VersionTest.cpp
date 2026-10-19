#include <gtest/gtest.h>
#include <string>
#include "flight_recorder/Version.h"

TEST(Version, IsSet) {
    EXPECT_EQ(std::string(flight_recorder::kVersion), "0.1.0-snapshot");
}
