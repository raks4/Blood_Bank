#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "types.h"
#include "blood.h"
#include "utils.h"
}

static Batch *append_test_batch(Database *db, const Batch *source)
{
    Batch *node = static_cast<Batch *>(std::malloc(sizeof(Batch)));
    if (node == nullptr) {
        return nullptr;
    }
    *node = *source;
    node->next = nullptr;
    Batch **link = &db->batches;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
    return node;
}

static Request *append_test_request(Database *db, const Request *source)
{
    Request *node = static_cast<Request *>(std::malloc(sizeof(Request)));
    if (node == nullptr) {
        return nullptr;
    }
    *node = *source;
    node->next = nullptr;
    Request **link = &db->requests;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
    return node;
}

static Batch *find_test_batch(Database *db, int id)
{
    Batch *batch = db->batches;
    while ((batch != nullptr) && (batch->id != id)) {
        batch = batch->next;
    }
    return batch;
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
    int next_batch_id = 0;
    EXPECT_EQ(record_donation(&db, &donor, 300, now + 1L, &next_batch_id), RESULT_OK);
    EXPECT_EQ(next_batch_id, 2);
    EXPECT_EQ(stock_for_group(&db, 0, now), 0);
    EXPECT_EQ(update_batch(&db, batch_id, BATCH_TEST, now), RESULT_OK);
    EXPECT_EQ(stock_for_group(&db, 0, now), 300);
    EXPECT_EQ(update_batch(&db, 99, BATCH_TEST, now), RESULT_NOT_FOUND);
    EXPECT_EQ(update_batch(&db, 99, BATCH_DISCARD, now), RESULT_NOT_FOUND);

    Batch *batch = find_test_batch(&db, batch_id);
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(update_batch(&db, batch_id, BATCH_DISCARD, now), RESULT_OK);
    EXPECT_EQ(batch->ml_left, 0);
    EXPECT_EQ(stock_for_group(&db, 0, now), 0);
    EXPECT_EQ(find_test_batch(&db, 99), nullptr);

    Batch trailing = {};
    trailing.id = 3;
    ASSERT_NE(append_test_batch(&db, &trailing), nullptr);

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
    ASSERT_NE(append_test_batch(&db, &older), nullptr);

    Batch newer = older;
    newer.id = 2;
    newer.ml_collected = 300;
    newer.ml_left = 300;
    newer.expiry = now + 200L;
    ASSERT_NE(append_test_batch(&db, &newer), nullptr);

    int request_id = 0;
    EXPECT_EQ(add_request(&db, "City Hospital", 0, 350, now, &request_id), RESULT_OK);
    EXPECT_EQ(request_id, 1);
    EXPECT_EQ(update_request(&db, request_id, REQUEST_FULFILL, now), RESULT_OK);
    EXPECT_EQ(find_test_batch(&db, 1)->ml_left, 0);
    EXPECT_EQ(find_test_batch(&db, 2)->ml_left, 150);
    EXPECT_EQ(update_request(&db, request_id, REQUEST_FULFILL, now), RESULT_WRONG_STATE);

    EXPECT_EQ(add_request(&db, "Another Hospital", GROUP_COUNT, 100, now, &request_id),
              RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "Another Hospital", 0, 500, now, &request_id), RESULT_OK);
    EXPECT_EQ(update_request(&db, request_id, REQUEST_FULFILL, now), RESULT_NOT_ENOUGH_STOCK);
    EXPECT_EQ(update_request(&db, request_id, REQUEST_REJECT, now), RESULT_OK);
    EXPECT_EQ(update_request(&db, request_id, REQUEST_REJECT, now), RESULT_WRONG_STATE);
    EXPECT_EQ(add_request(&db, "Third Hospital", 0, 50, now, &request_id), RESULT_OK);
    EXPECT_EQ(request_id, 3);

    ::testing::internal::CaptureStdout();
    list_requests(&db);
    const std::string output = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("fulfilled"), std::string::npos);
    EXPECT_NE(output.find("rejected"), std::string::npos);
    EXPECT_EQ(update_request(&db, 99, REQUEST_REJECT, now), RESULT_NOT_FOUND);

    free_database(&db);
    EXPECT_EQ(db.requests, nullptr);
}

TEST(BloodTests, ListsExpiredEmptyAndDiscardedBatchesAndHandlesInvalidRequests)
{
    Database db = {};
    const long now = 1000000L;
    Batch expired = {};
    expired.id = 1;
    expired.group = 0;
    expired.ml_collected = 200;
    expired.ml_left = 200;
    expired.expiry = now - 1L;
    expired.status = BATCH_AVAILABLE;
    ASSERT_NE(append_test_batch(&db, &expired), nullptr);

    Batch empty = expired;
    empty.id = 2;
    empty.expiry = now;
    empty.ml_left = 0;
    ASSERT_NE(append_test_batch(&db, &empty), nullptr);

    Batch discarded = empty;
    discarded.id = 3;
    discarded.status = BATCH_DISCARDED;
    discarded.ml_left = 100;
    ASSERT_NE(append_test_batch(&db, &discarded), nullptr);

    ::testing::internal::CaptureStdout();
    list_batches(&db, 0, now);
    const std::string listing = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(listing.find("expired"), std::string::npos);
    EXPECT_NE(listing.find("empty"), std::string::npos);
    EXPECT_NE(listing.find("discarded"), std::string::npos);

    int request_id = 0;
    EXPECT_EQ(add_request(&db, "Bad group", -1, 100, now, &request_id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "Bad amount", 0, 0, now, &request_id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "This hospital name is much too long to fit in the field",
                          0, 100, now, &request_id), RESULT_BAD_INPUT);
    EXPECT_EQ(update_request(&db, 99, REQUEST_FULFILL, now), RESULT_NOT_FOUND);
    EXPECT_EQ(update_request(&db, 99, REQUEST_REJECT, now), RESULT_NOT_FOUND);

    Request pending = {};
    pending.id = 1;
    pending.status = REQUEST_PENDING;
    (void)std::strcpy(pending.hospital, "Pending Hospital");
    ASSERT_NE(append_test_request(&db, &pending), nullptr);
    ::testing::internal::CaptureStdout();
    list_requests(&db);
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("pending"),
              std::string::npos);

    free_database(&db);
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
    ASSERT_NE(append_test_batch(&db, &batch), nullptr);

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
