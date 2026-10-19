#include <gtest/gtest.h>
#include "flight_recorder/Enable.h"

using namespace flight_recorder;

TEST(Enable, DisabledSuppressesAllSites) {
    disable();
    setCategoryMask(0xFFFFFFFFu);
    EXPECT_FALSE(enabled(Site::WorkerAppend));
}

TEST(Enable, CategoryMaskGatesBySite) {
    enable();
    setCategoryMask(categoryBit(Category::Produce)); // only Produce
    EXPECT_TRUE(enabled(Site::WorkerAppend));       // Produce
    EXPECT_FALSE(enabled(Site::StorageWrite));      // Storage
    setCategoryMask(categoryBit(Category::Storage));
    EXPECT_TRUE(enabled(Site::StorageWrite));
    EXPECT_FALSE(enabled(Site::WorkerAppend));
    disable(); // leave global state clean for other tests
}
