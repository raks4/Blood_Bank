#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "types.h"
#include "donors.h"
#include "blood.h"
#include "utils.h"
}

extern "C" {
extern bool g_fail_next_malloc;
extern size_t g_fail_malloc_size;
}

/* ------------------------------------------------------------------- */
/* Helpers                                                               */
/* ------------------------------------------------------------------- */
static Donor *append_test_donor(Database *db, int id, int group, int active)
{
    Donor *node = static_cast<Donor *>(std::calloc(1, sizeof(Donor)));
    node->id = id;
    node->group = group;
    node->active = active;
    node->birth_year = current_year() - 30;
    (void)std::snprintf(node->name, sizeof(node->name), "Donor %d", id);
    (void)std::snprintf(node->phone, sizeof(node->phone), "9876543%03d", id);
    Donor **link = &db->donors;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
    return node;
}

static void append_test_batch(Database *db, int donor_id)
{
    Batch *node = static_cast<Batch *>(std::calloc(1, sizeof(Batch)));
    node->donor_id = donor_id;
    Batch **link = &db->batches;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
}

static void free_test_donors(Database *db)
{
    while (db->donors != nullptr) {
        Donor *next = db->donors->next;
        std::free(db->donors);
        db->donors = next;
    }
}

static void free_test_batches(Database *db)
{
    while (db->batches != nullptr) {
        Batch *next = db->batches->next;
        std::free(db->batches);
        db->batches = next;
    }
}

/* ------------------------------------------------------------------- */
/* One test per function                                                 */
/* ------------------------------------------------------------------- */

TEST(DonorTests, FindDonor)
{
    Database db = {};
    EXPECT_EQ(find_donor(&db, 1), nullptr);                       /* empty list */

    Donor *first = append_test_donor(&db, 1, 0, 1);
    append_test_donor(&db, 2, 0, 1);
    Donor *last = append_test_donor(&db, 3, 0, 1);

    EXPECT_EQ(find_donor(&db, 1), first);                         /* head       */
    EXPECT_EQ(find_donor(&db, 3), last);                          /* tail       */
    EXPECT_EQ(find_donor(&db, 99), nullptr);                      /* not there  */

    free_test_donors(&db);
}

TEST(DonorTests, AddDonor)
{
    Database db = {};
    const int year = current_year();
    int id = 0;

    /* first, second and third donor: head, tail after one hop, tail after two hops */
    EXPECT_EQ(add_donor(&db, "John Smith", "9876543210", 0, year - 25, &id), RESULT_OK);
    EXPECT_EQ(id, 1);
    ASSERT_NE(db.donors, nullptr);
    EXPECT_STREQ(db.donors->name, "John Smith");
    EXPECT_STREQ(db.donors->phone, "9876543210");
    EXPECT_EQ(db.donors->group, 0);
    EXPECT_EQ(db.donors->birth_year, year - 25);
    EXPECT_EQ(db.donors->active, 1);
    EXPECT_EQ(db.donors->last_donation, 0L);
    EXPECT_GT(db.donors->registered_at, 0L);
    EXPECT_EQ(db.donors->deactivated_at, 0L);

    EXPECT_EQ(add_donor(&db, "Second", "1234567890", GROUP_COUNT - 1, year, &id), RESULT_OK);
    EXPECT_EQ(id, 2);
    EXPECT_EQ(add_donor(&db, "Third", "1234567891", 1, year - 40, &id), RESULT_OK);
    EXPECT_EQ(id, 3);
    ASSERT_NE(db.donors->next, nullptr);
    ASSERT_NE(db.donors->next->next, nullptr);
    EXPECT_EQ(db.donors->next->next->id, 3);
    EXPECT_EQ(add_donor(&db, "Duplicate Phone", "1234567890", 0, year - 25, &id),
              RESULT_EXISTS);
    EXPECT_EQ(id, 3);
    EXPECT_EQ(db.donors->next->next->next, nullptr);

    /* bad input: group, birth year in the future, name too long, phone too long */
    EXPECT_EQ(add_donor(&db, "X", "1234567890", -1, year - 25, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_donor(&db, "X", "1234567890", GROUP_COUNT, year - 25, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_donor(&db, "X", "1234567890", 0, year + 1, &id), RESULT_BAD_INPUT);
    const std::string long_name(NAME_LEN, 'n');                   /* needs NAME_LEN + 1 bytes  */
    EXPECT_EQ(add_donor(&db, long_name.c_str(), "1234567890", 0, year - 25, &id), RESULT_BAD_INPUT);
    const std::string long_phone(PHONE_LEN, '9');                 /* needs PHONE_LEN + 1 bytes */
    EXPECT_EQ(add_donor(&db, "X", long_phone.c_str(), 0, year - 25, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(id, 3);

    /* out of memory: nothing is added */
    g_fail_next_malloc = true;
    const Result failed = add_donor(&db, "No Memory", "4444444444", 0, year - 25, &id);
    g_fail_next_malloc = false;
    EXPECT_EQ(failed, RESULT_NO_MEMORY);
    EXPECT_EQ(id, 3);
    EXPECT_EQ(db.donors->next->next->next, nullptr);

    /* the new ID also looks at old batches, so IDs of deleted donors are not reused */
    append_test_batch(&db, 2);                                    /* lower than highest donor  */
    append_test_batch(&db, 20);                                   /* higher than every donor   */
    EXPECT_EQ(add_donor(&db, "After History", "5555555555", 0, year - 25, &id), RESULT_OK);
    EXPECT_EQ(id, 21);

    free_test_donors(&db);
    free_test_batches(&db);
}

TEST(DonorTests, UpdateDonor)
{
    Database db = {};
    append_test_donor(&db, 1, 0, 1);
    append_test_donor(&db, 2, 0, 1);
    append_test_donor(&db, 3, 0, 1);

    EXPECT_EQ(update_donor(&db, 99, DONOR_DEACTIVATE), RESULT_NOT_FOUND);
    EXPECT_EQ(update_donor(&db, 99, DONOR_DELETE), RESULT_NOT_FOUND);
    EXPECT_EQ(update_donor(&db, 1, (DonorAction)99), RESULT_BAD_INPUT);

    EXPECT_EQ(update_donor_details(&db, 99, "Changed", "1234567890", 1,
                                   current_year() - 25), RESULT_NOT_FOUND);
    EXPECT_EQ(update_donor_details(&db, 1, "Changed", "1234567890", GROUP_COUNT,
                                   current_year() - 25), RESULT_BAD_INPUT);
    const std::string too_long_name(NAME_LEN, 'n');
    EXPECT_EQ(update_donor_details(&db, 1, too_long_name.c_str(), "1234567890", 1,
                                   current_year() - 25), RESULT_BAD_INPUT);
    EXPECT_STREQ(find_donor(&db, 1)->name, "Donor 1");
    EXPECT_EQ(update_donor_details(&db, 1, "Changed", "9876543002", 1,
                                   current_year() - 25), RESULT_EXISTS);
    EXPECT_STREQ(find_donor(&db, 1)->name, "Donor 1");
    EXPECT_STREQ(find_donor(&db, 1)->phone, "9876543001");
    EXPECT_EQ(find_donor(&db, 1)->group, 0);
    EXPECT_EQ(update_donor_details(&db, 1, "Updated Donor", "1234567890", 1,
                                   current_year() - 25), RESULT_OK);
    EXPECT_STREQ(find_donor(&db, 1)->name, "Updated Donor");
    EXPECT_STREQ(find_donor(&db, 1)->phone, "1234567890");
    EXPECT_EQ(find_donor(&db, 1)->group, 1);
    EXPECT_EQ(find_donor(&db, 1)->birth_year, current_year() - 25);

    /* deactivate: ok, then wrong state */
    EXPECT_EQ(update_donor(&db, 2, DONOR_DEACTIVATE), RESULT_OK);
    EXPECT_EQ(find_donor(&db, 2)->active, 0);
    EXPECT_GT(find_donor(&db, 2)->deactivated_at, 0L);
    EXPECT_EQ(update_donor(&db, 2, DONOR_DEACTIVATE), RESULT_WRONG_STATE);

    /* delete from the middle, then the head, then the last one left */
    EXPECT_EQ(update_donor(&db, 2, DONOR_DELETE), RESULT_OK);
    EXPECT_EQ(find_donor(&db, 2), nullptr);
    EXPECT_EQ(db.donors->id, 1);
    EXPECT_EQ(db.donors->next->id, 3);
    EXPECT_EQ(update_donor(&db, 1, DONOR_DELETE), RESULT_OK);
    EXPECT_EQ(db.donors->id, 3);
    EXPECT_EQ(update_donor(&db, 3, DONOR_DELETE), RESULT_OK);
    EXPECT_EQ(db.donors, nullptr);
    EXPECT_EQ(update_donor(&db, 3, DONOR_DELETE), RESULT_NOT_FOUND);
}

TEST(DonorTests, UpdateDonorStatuses)
{
    Database db = {};
    const long now = 2000000000L;
    const long inactive_after = (long)DONOR_INACTIVE_DAYS * SECONDS_PER_DAY;
    const long delete_after = (long)DONOR_DELETION_DAYS * SECONDS_PER_DAY;

    EXPECT_EQ(update_donor_statuses(&db, now), 0);                /* empty list */

    Donor *d1 = append_test_donor(&db, 1, 0, 1);                  /* active, silent too long -> deactivated */
    d1->registered_at = now - inactive_after;
    Donor *d2 = append_test_donor(&db, 2, 0, 0);                  /* deactivated, no date -> kept           */
    d2->deactivated_at = 0L;
    Donor *d3 = append_test_donor(&db, 3, 0, 0);                  /* deactivated recently -> kept           */
    d3->deactivated_at = now - delete_after + 1L;
    Donor *d4 = append_test_donor(&db, 4, 0, 0);                  /* deactivated long ago -> deleted        */
    d4->deactivated_at = now - delete_after;
    Donor *d5 = append_test_donor(&db, 5, 0, 1);                  /* gave blood recently -> untouched       */
    d5->registered_at = now - inactive_after;
    d5->last_donation = now - SECONDS_PER_DAY;
    Donor *d6 = append_test_donor(&db, 6, 0, 1);                  /* never any activity date -> untouched   */
    d6->registered_at = 0L;
    d6->last_donation = 0L;
    Donor *d7 = append_test_donor(&db, 7, 0, 1);                  /* registered recently -> untouched       */
    d7->registered_at = now - SECONDS_PER_DAY;
    Donor *d8 = append_test_donor(&db, 8, 0, 1);                  /* old donation, silent too long          */
    d8->registered_at = now;
    d8->last_donation = now - inactive_after;

    EXPECT_EQ(update_donor_statuses(&db, now), 3);                /* d1, d8 deactivated + d4 deleted */

    EXPECT_EQ(find_donor(&db, 1)->active, 0);
    EXPECT_EQ(find_donor(&db, 1)->deactivated_at, now);
    EXPECT_EQ(find_donor(&db, 8)->active, 0);
    EXPECT_EQ(find_donor(&db, 8)->deactivated_at, now);
    EXPECT_NE(find_donor(&db, 2), nullptr);
    EXPECT_NE(find_donor(&db, 3), nullptr);
    EXPECT_EQ(find_donor(&db, 4), nullptr);
    EXPECT_EQ(find_donor(&db, 5)->active, 1);
    EXPECT_EQ(find_donor(&db, 6)->active, 1);
    EXPECT_EQ(find_donor(&db, 7)->active, 1);

    /* running again straight away changes nothing */
    EXPECT_EQ(update_donor_statuses(&db, now), 0);

    free_test_donors(&db);
}

TEST(DonorTests, DonorProblem)
{
    const long now = 2000000000L;
    const long wait = (long)DAYS_BETWEEN_DONATIONS * SECONDS_PER_DAY;
    Donor donor = {};
    donor.active = 1;
    donor.birth_year = current_year() - 30;

    EXPECT_EQ(donor_problem(&donor, now), nullptr);               /* never donated, eligible */

    donor.last_donation = now - wait;                             /* exactly long enough ago */
    EXPECT_EQ(donor_problem(&donor, now), nullptr);
    donor.last_donation = now - wait + 1L;                        /* one second too soon     */
    EXPECT_STREQ(donor_problem(&donor, now), "This donor gave blood less than 56 days ago.");
    donor.last_donation = 0L;

    donor.birth_year = current_year() - (MIN_AGE - 1);            /* too young               */
    EXPECT_STREQ(donor_problem(&donor, now), "Donors must be between 18 and 65 years old.");
    donor.birth_year = current_year() - (MAX_AGE + 1);            /* too old                 */
    EXPECT_STREQ(donor_problem(&donor, now), "Donors must be between 18 and 65 years old.");
    donor.birth_year = current_year() - MIN_AGE;                  /* youngest allowed        */
    EXPECT_EQ(donor_problem(&donor, now), nullptr);
    donor.birth_year = current_year() - MAX_AGE;                  /* oldest allowed          */
    EXPECT_EQ(donor_problem(&donor, now), nullptr);

    donor.active = 0;                                             /* deactivated wins        */
    EXPECT_STREQ(donor_problem(&donor, now), "This donor is deactivated.");
}

TEST(DonorTests, ListDonors)
{
    Database db = {};

    ::testing::internal::CaptureStdout();
    list_donors(&db, -1);                                         /* empty list */
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("0 donor(s) shown."),
              std::string::npos);

    append_test_donor(&db, 1, 0, 1);
    append_test_donor(&db, 2, 1, 0);                              /* deactivated */
    append_test_donor(&db, 3, 99, 1);                             /* invalid group name -> "?" */

    ::testing::internal::CaptureStdout();
    list_donors(&db, -1);                                         /* everybody */
    const std::string all = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(all.find("Donor 1"), std::string::npos);
    EXPECT_NE(all.find("Donor 2"), std::string::npos);
    EXPECT_NE(all.find("Donor 3"), std::string::npos);
    EXPECT_NE(all.find("A+"), std::string::npos);
    EXPECT_NE(all.find("A-"), std::string::npos);
    EXPECT_NE(all.find("?"), std::string::npos);
    EXPECT_NE(all.find("deactivated"), std::string::npos);
    EXPECT_NE(all.find("active"), std::string::npos);
    EXPECT_NE(all.find("3 donor(s) shown."), std::string::npos);

    ::testing::internal::CaptureStdout();
    list_donors(&db, 1);                                          /* one group */
    const std::string filtered = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(filtered.find("Donor 2"), std::string::npos);
    EXPECT_EQ(filtered.find("Donor 1"), std::string::npos);
    EXPECT_NE(filtered.find("1 donor(s) shown."), std::string::npos);

    ::testing::internal::CaptureStdout();
    list_donors(&db, 5);                                          /* no match  */
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("0 donor(s) shown."),
              std::string::npos);

    free_test_donors(&db);
}
