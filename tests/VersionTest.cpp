#include <gtest/gtest.h>
#include <string>
#include "rune_fr/Version.h"

TEST(Version, IsSet) {
    EXPECT_EQ(std::string(rune_fr::kVersion), "0.1.0-snapshot");
}
