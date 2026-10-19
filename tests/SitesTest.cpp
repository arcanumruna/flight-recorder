#include <gtest/gtest.h>
#include <string>
#include "flight_recorder/FrRecord.h"
#include "flight_recorder/Sites.h"

TEST(FrRecord, IsThirtyTwoBytes) {   // 24-byte v1 + the per-event double value (37e3856)
    EXPECT_EQ(sizeof(flight_recorder::FrRecord), 32u);
}

TEST(FrRecord, DurationNeverUnderflows) {
    flight_recorder::FrRecord r{100, 90, 0, 0, 0}; // end before start
    EXPECT_EQ(r.durationTsc(), 0u);
    flight_recorder::FrRecord eq{100, 100, 0, 0, 0}; // instant
    EXPECT_EQ(eq.durationTsc(), 0u);
    flight_recorder::FrRecord span{100, 175, 0, 0, 0};
    EXPECT_EQ(span.durationTsc(), 75u);
}

TEST(Sites, CategoryAndNameLookup) {
    EXPECT_EQ(flight_recorder::categoryOf(flight_recorder::Site::WorkerAppend), flight_recorder::Category::Produce);
    EXPECT_EQ(flight_recorder::categoryOf(flight_recorder::Site::StorageWrite), flight_recorder::Category::Storage);
    EXPECT_EQ(std::string(flight_recorder::nameOf(flight_recorder::Site::EgressSend)), "EgressSend");
}

TEST(Sites, MetaTableCoversEverySite) {
    EXPECT_EQ(sizeof(flight_recorder::kSiteMeta) / sizeof(flight_recorder::kSiteMeta[0]),
              static_cast<size_t>(flight_recorder::Site::Count));
}

TEST(Sites, NamesAreUniqueAndNonEmpty) {
    const size_t n = static_cast<size_t>(flight_recorder::Site::Count);
    for (size_t i = 0; i < n; ++i) {
        ASSERT_NE(std::string(flight_recorder::kSiteMeta[i].name), "");
        for (size_t j = i + 1; j < n; ++j)
            ASSERT_NE(std::string(flight_recorder::kSiteMeta[i].name), std::string(flight_recorder::kSiteMeta[j].name)) << i << " " << j;
    }
}

TEST(Sites, HopSitesHaveTheirCategories) {   // the category selects the compile flag
    using flight_recorder::Category;
    using flight_recorder::Site;
    EXPECT_EQ(flight_recorder::categoryOf(Site::ShIngressFrame), Category::Produce);
    EXPECT_EQ(flight_recorder::categoryOf(Site::ShAppendCorr), Category::Produce);
    EXPECT_EQ(flight_recorder::categoryOf(Site::ShWaiterFire), Category::Replication);
    EXPECT_EQ(flight_recorder::categoryOf(Site::ShStageAckFlush), Category::Runtime);
    EXPECT_EQ(flight_recorder::categoryOf(Site::StorageFsync), Category::Storage);
    EXPECT_EQ(flight_recorder::categoryOf(Site::ClientRetransmit), Category::Produce);
    EXPECT_EQ(std::string(flight_recorder::nameOf(Site::ClientRetriable)), "ClientRetriable");
    EXPECT_EQ(std::string(flight_recorder::nameOf(Site::AeronRecvDispatch)), "AeronRecvDispatch");
}
