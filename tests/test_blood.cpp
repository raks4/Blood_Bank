#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "types.h"
#include "blood.h"
#include "utils.h"
}

TEST(BloodTests, DonationBatchAndStockLifecycle)
{
    Database db = {};
    Donor donor = {};
    donor.id = 7;
    donor.group = 0;
    const long now = 1000000L;
    int batch_id = 0;

    EXPECT_EQ(record_donation(&db, &donor, MIN_DONATION_ML - 1, now, &batch_id),
              RESULT_BAD_INPUT);
    EXPECT_EQ(record_donation(&db, &donor, 300, now, &batch_id), RESULT_OK);
    EXPECT_EQ(batch_id, 1);
    EXPECT_EQ(donor.last_donation, now);
    EXPECT_EQ(stock_for_group(&db, 0, now), 0);
    EXPECT_EQ(mark_batch_tested(&db, batch_id, now), RESULT_OK);
    EXPECT_EQ(stock_for_group(&db, 0, now), 300);

    Batch *batch = find_batch(&db, batch_id);
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(discard_batch(&db, batch_id), RESULT_OK);
    EXPECT_EQ(batch->ml_left, 0);
    EXPECT_EQ(stock_for_group(&db, 0, now), 0);
    EXPECT_EQ(find_batch(&db, 99), nullptr);

    free_database(&db);
    EXPECT_EQ(db.batches, nullptr);
}

TEST(BloodTests, RequestsUseOldestStockAndTrackStatuses)
{
    Database db = {};
    const long now = 1000000L;
    Batch older = {};
    older.id = 1;
    older.group = 0;
    older.ml_collected = 200;
    older.ml_left = 200;
    older.expiry = now + 100L;
    older.status = BATCH_AVAILABLE;
    ASSERT_NE(append_batch(&db, &older), nullptr);

    Batch newer = older;
    newer.id = 2;
    newer.ml_collected = 300;
    newer.ml_left = 300;
    newer.expiry = now + 200L;
    ASSERT_NE(append_batch(&db, &newer), nullptr);

    int request_id = 0;
    EXPECT_EQ(add_request(&db, "City Hospital", 0, 350, now, &request_id), RESULT_OK);
    EXPECT_EQ(request_id, 1);
    EXPECT_EQ(process_request(&db, request_id, now), RESULT_OK);
    EXPECT_EQ(find_batch(&db, 1)->ml_left, 0);
    EXPECT_EQ(find_batch(&db, 2)->ml_left, 150);
    EXPECT_EQ(process_request(&db, request_id, now), RESULT_WRONG_STATE);

    EXPECT_EQ(add_request(&db, "Another Hospital", GROUP_COUNT, 100, now, &request_id),
              RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "Another Hospital", 0, 500, now, &request_id), RESULT_OK);
    EXPECT_EQ(process_request(&db, request_id, now), RESULT_NOT_ENOUGH_STOCK);
    EXPECT_EQ(reject_request(&db, request_id), RESULT_OK);
    EXPECT_EQ(reject_request(&db, request_id), RESULT_WRONG_STATE);

    ::testing::internal::CaptureStdout();
    list_requests(&db);
    const std::string output = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("fulfilled"), std::string::npos);
    EXPECT_NE(output.find("rejected"), std::string::npos);
    EXPECT_EQ(reject_request(&db, 99), RESULT_NOT_FOUND);

    free_database(&db);
    EXPECT_EQ(db.requests, nullptr);
}

TEST(BloodTests, BatchListingAndStockReportShowRelevantRows)
{
    Database db = {};
    Batch batch = {};
    batch.id = 1;
    batch.donor_id = 10;
    batch.group = 0;
    batch.ml_collected = 1200;
    batch.ml_left = 1200;
    batch.collected = 1000000L;
    batch.expiry = 2000000L;
    batch.status = BATCH_AVAILABLE;
    ASSERT_NE(append_batch(&db, &batch), nullptr);

    ::testing::internal::CaptureStdout();
    show_stock(&db, 1500000L);
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("1200"), std::string::npos);

    ::testing::internal::CaptureStdout();
    list_batches(&db, 10, 1500000L);
    const std::string donor_batches = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(donor_batches.find("available"), std::string::npos);
    EXPECT_NE(donor_batches.find("1 batch(es) shown."), std::string::npos);

    ::testing::internal::CaptureStdout();
    list_batches(&db, 99, 1500000L);
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("0 batch(es) shown."),
              std::string::npos);

    free_database(&db);
}
