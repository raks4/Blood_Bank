#include <gtest/gtest.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "types.h"
#include "blood.h"
#include "utils.h"
}

extern "C" {
extern bool g_fail_next_malloc;
extern size_t g_fail_malloc_size;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static const long NOW = 1000000L;

static Batch *append_test_batch(Database *db, int id, int donor_id, int group, int ml_left,
                                long expiry, int status)
{
    Batch *node = static_cast<Batch *>(std::calloc(1, sizeof(Batch)));
    node->id = id;
    node->donor_id = donor_id;
    node->group = group;
    node->ml_collected = ml_left;
    node->ml_left = ml_left;
    node->collected = NOW;
    node->expiry = expiry;
    node->status = status;
    Batch **link = &db->batches;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
    return node;
}

static Request *append_test_request(Database *db, int id, int status, long date)
{
    Request *node = static_cast<Request *>(std::calloc(1, sizeof(Request)));
    node->id = id;
    node->status = status;
    node->date = date;
    (void)std::strcpy(node->hospital, "Test Hospital");
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

/* ------------------------------------------------------------------ */
/* One test per function                                               */
/* ------------------------------------------------------------------ */

TEST(BloodTests, RecordDonation)
{
    Database db = {};
    Donor donor = {};
    donor.id = 7;
    donor.group = 2;
    int batch_id = 0;

    /* amount out of range: too low, too high */
    EXPECT_EQ(record_donation(&db, &donor, MIN_DONATION_ML - 1, NOW, &batch_id), RESULT_BAD_INPUT);
    EXPECT_EQ(record_donation(&db, &donor, MAX_DONATION_ML + 1, NOW, &batch_id), RESULT_BAD_INPUT);
    EXPECT_EQ(db.batches, nullptr);

    /* first donation: becomes the list head, waits for tests */
    EXPECT_EQ(record_donation(&db, &donor, MIN_DONATION_ML, NOW, &batch_id), RESULT_OK);
    EXPECT_EQ(batch_id, 1);
    EXPECT_EQ(donor.last_donation, NOW);
    ASSERT_NE(db.batches, nullptr);
    EXPECT_EQ(db.batches->donor_id, 7);
    EXPECT_EQ(db.batches->group, 2);
    EXPECT_EQ(db.batches->ml_left, MIN_DONATION_ML);
    EXPECT_EQ(db.batches->status, BATCH_WAITING);
    EXPECT_EQ(db.batches->expiry, NOW + (long)SHELF_LIFE_DAYS * SECONDS_PER_DAY);

    /* second donation: appended at the tail, gets the next id */
    EXPECT_EQ(record_donation(&db, &donor, MAX_DONATION_ML, NOW + 5L, &batch_id), RESULT_OK);
    EXPECT_EQ(batch_id, 2);
    EXPECT_EQ(donor.last_donation, NOW + 5L);
    ASSERT_NE(db.batches->next, nullptr);
    EXPECT_EQ(db.batches->next->id, 2);

    /* third donation: walks past two existing nodes to reach the tail */
    EXPECT_EQ(record_donation(&db, &donor, MIN_DONATION_ML, NOW + 6L, &batch_id), RESULT_OK);
    EXPECT_EQ(batch_id, 3);
    ASSERT_NE(db.batches->next->next, nullptr);
    EXPECT_EQ(db.batches->next->next->id, 3);

    /* out of memory: nothing changes */
    g_fail_next_malloc = true;
    const Result failed = record_donation(&db, &donor, MIN_DONATION_ML, NOW + 9L, &batch_id);
    g_fail_next_malloc = false;
    EXPECT_EQ(failed, RESULT_NO_MEMORY);
    EXPECT_EQ(batch_id, 3);
    EXPECT_EQ(donor.last_donation, NOW + 6L);

    free_database(&db);
}

TEST(BloodTests, UpdateBatch)
{
    Database db = {};
    append_test_batch(&db, 1, 1, 0, 100, NOW + 100L, BATCH_WAITING);   /* normal waiting batch  */
    append_test_batch(&db, 2, 1, 0, 100, NOW - 1L, BATCH_WAITING);     /* waiting but expired   */
    append_test_batch(&db, 3, 1, 0, 100, NOW + 100L, BATCH_AVAILABLE); /* already tested        */

    EXPECT_EQ(update_batch(&db, 99, BATCH_TEST, NOW), RESULT_NOT_FOUND);
    EXPECT_EQ(update_batch(&db, 1, (BatchAction)99, NOW), RESULT_BAD_INPUT);

    /* BATCH_TEST: ok, then wrong state (already available), expired */
    EXPECT_EQ(update_batch(&db, 1, BATCH_TEST, NOW), RESULT_OK);
    EXPECT_EQ(find_test_batch(&db, 1)->status, BATCH_AVAILABLE);
    EXPECT_EQ(update_batch(&db, 3, BATCH_TEST, NOW), RESULT_WRONG_STATE);
    EXPECT_EQ(update_batch(&db, 2, BATCH_TEST, NOW), RESULT_WRONG_STATE);
    EXPECT_EQ(find_test_batch(&db, 2)->status, BATCH_WAITING);

    /* BATCH_DISCARD: ok, then wrong state (already discarded) */
    EXPECT_EQ(update_batch(&db, 1, BATCH_DISCARD, NOW), RESULT_OK);
    EXPECT_EQ(find_test_batch(&db, 1)->status, BATCH_DISCARDED);
    EXPECT_EQ(find_test_batch(&db, 1)->ml_left, 0);
    EXPECT_EQ(update_batch(&db, 1, BATCH_DISCARD, NOW), RESULT_WRONG_STATE);

    free_database(&db);
}

TEST(BloodTests, StockForGroup)
{
    Database db = {};
    EXPECT_EQ(stock_for_group(&db, 0, NOW), 0);                         /* empty database */

    append_test_batch(&db, 1, 1, 0, 100, NOW + 100L, BATCH_AVAILABLE);  /* counted           */
    append_test_batch(&db, 2, 1, 0, 250, NOW,        BATCH_AVAILABLE);  /* counted (expiry == now) */
    append_test_batch(&db, 3, 1, 1, 400, NOW + 100L, BATCH_AVAILABLE);  /* other group       */
    append_test_batch(&db, 4, 1, 0, 500, NOW + 100L, BATCH_WAITING);    /* not tested yet    */
    append_test_batch(&db, 5, 1, 0, 600, NOW - 1L,   BATCH_AVAILABLE);  /* expired           */
    append_test_batch(&db, 6, 1, 0, 0,   NOW + 100L, BATCH_AVAILABLE);  /* empty             */
    append_test_batch(&db, 7, 1, 0, 700, NOW + 100L, BATCH_DISCARDED);  /* discarded         */

    EXPECT_EQ(stock_for_group(&db, 0, NOW), 350);
    EXPECT_EQ(stock_for_group(&db, 1, NOW), 400);
    EXPECT_EQ(stock_for_group(&db, 2, NOW), 0);

    free_database(&db);
}

TEST(BloodTests, ShowStock)
{
    Database db = {};
    append_test_batch(&db, 1, 1, 0, LOW_STOCK_ML, NOW + 100L, BATCH_AVAILABLE); /* group 0: not low   */
    append_test_batch(&db, 2, 1, 1, 123, NOW + 100L, BATCH_WAITING);            /* group 1: waiting   */
    append_test_batch(&db, 3, 1, 1, 777, NOW - 1L,   BATCH_WAITING);            /* waiting + expired  */
    append_test_batch(&db, 4, 1, 2, 555, NOW + 100L, BATCH_AVAILABLE);          /* other group        */

    ::testing::internal::CaptureStdout();
    show_stock(&db, NOW);
    const std::string out = ::testing::internal::GetCapturedStdout();

    EXPECT_NE(out.find("Ready to use"), std::string::npos);
    EXPECT_NE(out.find("A+"), std::string::npos);
    EXPECT_NE(out.find("O-"), std::string::npos);
    EXPECT_NE(out.find("LOW STOCK"), std::string::npos);   /* most groups are low */
    EXPECT_NE(out.find("123"), std::string::npos);         /* waiting ml counted  */
    EXPECT_EQ(out.find("777"), std::string::npos);         /* expired not counted */

    free_database(&db);
}

TEST(BloodTests, ListBatches)
{
    Database db = {};
    append_test_batch(&db, 1, 10, 0, 100, NOW + 100L, BATCH_AVAILABLE);
    append_test_batch(&db, 2, 20, 1, 100, NOW + 100L, BATCH_WAITING);
    append_test_batch(&db, 3, 20, 2, 100, NOW - 1L,   BATCH_AVAILABLE);   /* expired   */
    append_test_batch(&db, 4, 20, 3, 100, NOW + 100L, BATCH_DISCARDED);
    append_test_batch(&db, 5, 20, 4, 0,   NOW + 100L, BATCH_AVAILABLE);   /* empty     */
    append_test_batch(&db, 6, 30, -1, 100, NOW + 100L, BATCH_AVAILABLE);  /* bad group -> "?" */
    Batch *overflow = append_test_batch(&db, 7, 40, 5, 100, NOW + 100L, BATCH_AVAILABLE);
    overflow->collected = LONG_MAX;                                       /* date cannot be formatted */

    ::testing::internal::CaptureStdout();
    list_batches(&db, 0, NOW);                                            /* all donors */
    const std::string all = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(all.find("available"), std::string::npos);
    EXPECT_NE(all.find("waiting for tests"), std::string::npos);
    EXPECT_NE(all.find("expired"), std::string::npos);
    EXPECT_NE(all.find("discarded"), std::string::npos);
    EXPECT_NE(all.find("empty"), std::string::npos);
    EXPECT_NE(all.find("?"), std::string::npos);
    EXPECT_NE(all.find("unknown"), std::string::npos);
    EXPECT_NE(all.find("7 batch(es) shown."), std::string::npos);

    ::testing::internal::CaptureStdout();
    list_batches(&db, 10, NOW);                                           /* one donor  */
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("1 batch(es) shown."),
              std::string::npos);

    ::testing::internal::CaptureStdout();
    list_batches(&db, 99, NOW);                                           /* no match   */
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("0 batch(es) shown."),
              std::string::npos);

    free_database(&db);
}

TEST(BloodTests, AddRequest)
{
    Database db = {};
    int id = 0;

    EXPECT_EQ(add_request(&db, "City Hospital", 0, MIN_REQUEST_ML, NOW, &id), RESULT_OK);
    EXPECT_EQ(id, 1);
    ASSERT_NE(db.requests, nullptr);
    EXPECT_STREQ(db.requests->hospital, "City Hospital");
    EXPECT_EQ(db.requests->status, REQUEST_PENDING);

    /* second request goes to the tail with the next id */
    EXPECT_EQ(add_request(&db, "Second", GROUP_COUNT - 1, MAX_REQUEST_ML, NOW, &id), RESULT_OK);
    EXPECT_EQ(id, 2);
    ASSERT_NE(db.requests->next, nullptr);
    EXPECT_EQ(db.requests->next->id, 2);

    /* third request: walks past two existing nodes to reach the tail */
    EXPECT_EQ(add_request(&db, "Third", 1, MIN_REQUEST_ML, NOW, &id), RESULT_OK);
    EXPECT_EQ(id, 3);
    ASSERT_NE(db.requests->next->next, nullptr);
    EXPECT_EQ(db.requests->next->next->id, 3);

    /* bad input: group, amount, hospital name */
    EXPECT_EQ(add_request(&db, "X", -1, MIN_REQUEST_ML, NOW, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "X", GROUP_COUNT, MIN_REQUEST_ML, NOW, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "X", 0, MIN_REQUEST_ML - 1, NOW, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_request(&db, "X", 0, MAX_REQUEST_ML + 1, NOW, &id), RESULT_BAD_INPUT);
    const std::string too_long(NAME_LEN, 'h');                            /* needs NAME_LEN + 1 bytes */
    EXPECT_EQ(add_request(&db, too_long.c_str(), 0, MIN_REQUEST_ML, NOW, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(id, 3);

    /* out of memory */
    g_fail_next_malloc = true;
    const Result failed = add_request(&db, "No Memory", 0, MIN_REQUEST_ML, NOW, &id);
    g_fail_next_malloc = false;
    EXPECT_EQ(failed, RESULT_NO_MEMORY);
    EXPECT_EQ(id, 3);

    free_database(&db);
}

TEST(BloodTests, UpdateRequest)
{
    Database db = {};
    append_test_batch(&db, 1, 1, 0, 200, NOW + 100L, BATCH_AVAILABLE);    /* expires first  */
    append_test_batch(&db, 2, 1, 0, 300, NOW + 200L, BATCH_AVAILABLE);
    int id = 0;

    EXPECT_EQ(update_request(&db, 99, REQUEST_FULFILL, NOW), RESULT_NOT_FOUND);

    ASSERT_EQ(add_request(&db, "City", 0, 350, NOW, &id), RESULT_OK);
    const int fulfil_id = id;
    EXPECT_EQ(update_request(&db, fulfil_id, (RequestAction)99, NOW), RESULT_BAD_INPUT);

    /* fulfil: oldest batch emptied first, rest from the next one */
    EXPECT_EQ(update_request(&db, fulfil_id, REQUEST_FULFILL, NOW), RESULT_OK);
    EXPECT_EQ(find_test_batch(&db, 1)->ml_left, 0);
    EXPECT_EQ(find_test_batch(&db, 2)->ml_left, 150);
    EXPECT_EQ(db.requests->status, REQUEST_FULFILLED);
    EXPECT_EQ(update_request(&db, fulfil_id, REQUEST_FULFILL, NOW), RESULT_WRONG_STATE);
    EXPECT_EQ(update_request(&db, fulfil_id, REQUEST_REJECT, NOW), RESULT_WRONG_STATE);

    /* not enough stock: nothing is taken, request stays pending */
    ASSERT_EQ(add_request(&db, "Big", 0, 500, NOW, &id), RESULT_OK);
    const int big_id = id;
    EXPECT_EQ(update_request(&db, big_id, REQUEST_FULFILL, NOW), RESULT_NOT_ENOUGH_STOCK);
    EXPECT_EQ(find_test_batch(&db, 2)->ml_left, 150);

    /* reject */
    EXPECT_EQ(update_request(&db, big_id, REQUEST_REJECT, NOW), RESULT_OK);
    EXPECT_EQ(update_request(&db, big_id, REQUEST_REJECT, NOW), RESULT_WRONG_STATE);

    free_database(&db);
}

TEST(BloodTests, ListRequests)
{
    Database db = {};

    ::testing::internal::CaptureStdout();
    list_requests(&db);                                                   /* empty list */
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("0 request(s) shown."),
              std::string::npos);

    append_test_request(&db, 1, REQUEST_PENDING, NOW);
    append_test_request(&db, 2, REQUEST_FULFILLED, NOW);
    append_test_request(&db, 3, REQUEST_REJECTED, NOW);
    Request *odd = append_test_request(&db, 4, REQUEST_PENDING, LONG_MAX); /* bad date + bad group */
    odd->group = -1;

    ::testing::internal::CaptureStdout();
    list_requests(&db);
    const std::string out = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("pending"), std::string::npos);
    EXPECT_NE(out.find("fulfilled"), std::string::npos);
    EXPECT_NE(out.find("rejected"), std::string::npos);
    EXPECT_NE(out.find("unknown"), std::string::npos);
    EXPECT_NE(out.find("4 request(s) shown."), std::string::npos);

    free_database(&db);
}

TEST(BloodTests, FreeDatabase)
{
    Database db = {};

    /* nothing allocated: must be a harmless no-op */
    free_database(&db);
    EXPECT_EQ(db.donors, nullptr);
    EXPECT_EQ(db.batches, nullptr);
    EXPECT_EQ(db.requests, nullptr);
    EXPECT_EQ(db.users, nullptr);

    /* every list populated with more than one node */
    for (int i = 0; i < 2; i++) {
        Donor *donor = static_cast<Donor *>(std::calloc(1, sizeof(Donor)));
        donor->next = db.donors;
        db.donors = donor;

        User *user = static_cast<User *>(std::calloc(1, sizeof(User)));
        user->next = db.users;
        db.users = user;
    }
    append_test_batch(&db, 1, 1, 0, 100, NOW, BATCH_AVAILABLE);
    append_test_batch(&db, 2, 1, 0, 100, NOW, BATCH_AVAILABLE);
    append_test_request(&db, 1, REQUEST_PENDING, NOW);
    append_test_request(&db, 2, REQUEST_PENDING, NOW);

    free_database(&db);
    EXPECT_EQ(db.donors, nullptr);
    EXPECT_EQ(db.batches, nullptr);
    EXPECT_EQ(db.requests, nullptr);
    EXPECT_EQ(db.users, nullptr);
}
