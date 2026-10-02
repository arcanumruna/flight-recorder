#include <gtest/gtest.h>
#include <string>
#include "rune_fr/FrRecord.h"
#include "rune_fr/Sites.h"

TEST(FrRecord, IsThirtyTwoBytes) {   // 24-byte v1 + the per-event double value (37e3856)
    EXPECT_EQ(sizeof(rune_fr::FrRecord), 32u);
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

TEST(Sites, NamesAreUniqueAndNonEmpty) {
    const size_t n = static_cast<size_t>(rune_fr::Site::Count);
    for (size_t i = 0; i < n; ++i) {
        ASSERT_NE(std::string(rune_fr::kSiteMeta[i].name), "");
        for (size_t j = i + 1; j < n; ++j)
            ASSERT_NE(std::string(rune_fr::kSiteMeta[i].name), std::string(rune_fr::kSiteMeta[j].name)) << i << " " << j;
    }
}

TEST(Sites, HopSitesHaveTheirCategories) {   // the category selects the compile flag
    using rune_fr::Category;
    using rune_fr::Site;
    EXPECT_EQ(rune_fr::categoryOf(Site::ShIngressFrame), Category::Produce);
    EXPECT_EQ(rune_fr::categoryOf(Site::ShAppendCorr), Category::Produce);
    EXPECT_EQ(rune_fr::categoryOf(Site::ShWaiterFire), Category::Replication);
    EXPECT_EQ(rune_fr::categoryOf(Site::ShStageAckFlush), Category::Runtime);
    EXPECT_EQ(rune_fr::categoryOf(Site::StorageFsync), Category::Storage);
    EXPECT_EQ(rune_fr::categoryOf(Site::ClientRetransmit), Category::Produce);
    EXPECT_EQ(std::string(rune_fr::nameOf(Site::ClientRetriable)), "ClientRetriable");
    EXPECT_EQ(std::string(rune_fr::nameOf(Site::AeronRecvDispatch)), "AeronRecvDispatch");
}
