#include <gtest/gtest.h>
#include <string>
#include "rune_fr/FrRecord.h"
#include "rune_fr/Sites.h"

TEST(FrRecord, IsTwentyFourBytes) {
    EXPECT_EQ(sizeof(rune_fr::FrRecord), 24u);
}

TEST(FrRecord, DurationNeverUnderflows) {
    rune_fr::FrRecord r{100, 90, 0, 0, 0}; // end before start
    EXPECT_EQ(r.durationTsc(), 0u);
    rune_fr::FrRecord eq{100, 100, 0, 0, 0}; // instant
    EXPECT_EQ(eq.durationTsc(), 0u);
    rune_fr::FrRecord span{100, 175, 0, 0, 0};
    EXPECT_EQ(span.durationTsc(), 75u);
}

TEST(Sites, CategoryAndNameLookup) {
    EXPECT_EQ(rune_fr::categoryOf(rune_fr::Site::WorkerAppend), rune_fr::Category::Produce);
    EXPECT_EQ(rune_fr::categoryOf(rune_fr::Site::StorageWrite), rune_fr::Category::Storage);
    EXPECT_EQ(std::string(rune_fr::nameOf(rune_fr::Site::EgressSend)), "EgressSend");
}

TEST(Sites, MetaTableCoversEverySite) {
    EXPECT_EQ(sizeof(rune_fr::kSiteMeta) / sizeof(rune_fr::kSiteMeta[0]),
              static_cast<size_t>(rune_fr::Site::Count));
}
