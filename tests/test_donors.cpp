#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "types.h"
#include "donors.h"
#include "blood.h"
#include "utils.h"
}

TEST(DonorTests, DonorLifecycleAndEligibility)
{
    Database db = {};
    Donor source = {};
    source.id = 8;
    (void)std::strcpy(source.name, "Existing");
    ASSERT_NE(append_donor(&db, &source), nullptr);

    int id = 0;
    EXPECT_EQ(add_donor(&db, "John Smith", "9876543210", 0,
                        current_year() - 25, &id), RESULT_OK);
    EXPECT_EQ(id, 9);

    Donor *donor = find_donor(&db, id);
    ASSERT_NE(donor, nullptr);
    EXPECT_STREQ(donor->name, "John Smith");
    EXPECT_GT(donor->registered_at, 0L);
    EXPECT_EQ(donor->deactivated_at, 0L);
    EXPECT_EQ(find_donor(&db, 10), nullptr);
    EXPECT_EQ(add_donor(&db, "Invalid", "1234567890", GROUP_COUNT,
                        current_year() - 25, &id), RESULT_BAD_INPUT);
    EXPECT_EQ(add_donor(&db, "Future", "1234567890", 0,
                        current_year() + 1, &id), RESULT_BAD_INPUT);

    EXPECT_EQ(donor_problem(donor, 1000000L), nullptr);
    donor->last_donation = 1000000L;
    EXPECT_NE(donor_problem(donor, 1000001L), nullptr);
    donor->last_donation = 0L;
    EXPECT_EQ(deactivate_donor(&db, id), RESULT_OK);
    EXPECT_STREQ(donor_problem(donor, 1000000L), "This donor is deactivated.");
    EXPECT_EQ(deactivate_donor(&db, id), RESULT_WRONG_STATE);
    EXPECT_EQ(deactivate_donor(&db, 99), RESULT_NOT_FOUND);

    while (db.donors != nullptr) {
        Donor *next = db.donors->next;
        std::free(db.donors);
        db.donors = next;
    }
}

TEST(DonorTests, InactivityDeactivatesAndDeletesOnTheConfiguredSchedule)
{
    Database db = {};
    const long now = 2000000000L;
    const long six_months = (long)DONOR_INACTIVE_DAYS * SECONDS_PER_DAY;
    const long one_month = (long)DONOR_DELETION_DAYS * SECONDS_PER_DAY;

    Donor active = {};
    active.id = 1;
    active.active = 1;
    active.registered_at = now - six_months;
    ASSERT_NE(append_donor(&db, &active), nullptr);

    Donor recently_inactive = {};
    recently_inactive.id = 2;
    recently_inactive.deactivated_at = now - one_month + 1L;
    ASSERT_NE(append_donor(&db, &recently_inactive), nullptr);

    Donor recently_active = {};
    recently_active.id = 4;
    recently_active.active = 1;
    recently_active.registered_at = now - six_months;
    recently_active.last_donation = now - SECONDS_PER_DAY;
    ASSERT_NE(append_donor(&db, &recently_active), nullptr);

    Donor due_for_deletion = {};
    due_for_deletion.id = 3;
    due_for_deletion.deactivated_at = now - one_month;
    ASSERT_NE(append_donor(&db, &due_for_deletion), nullptr);

    EXPECT_EQ(update_donor_statuses(&db, now), 2);
    ASSERT_NE(find_donor(&db, 1), nullptr);
    EXPECT_EQ(find_donor(&db, 1)->active, 0);
    EXPECT_EQ(find_donor(&db, 1)->deactivated_at, now);
    EXPECT_NE(find_donor(&db, 2), nullptr);
    ASSERT_NE(find_donor(&db, 4), nullptr);
    EXPECT_EQ(find_donor(&db, 4)->active, 1);
    EXPECT_EQ(find_donor(&db, 3), nullptr);

    EXPECT_EQ(delete_donor(&db, 2), RESULT_OK);
    EXPECT_EQ(delete_donor(&db, 2), RESULT_NOT_FOUND);
    EXPECT_EQ(delete_donor(&db, 1), RESULT_OK);
    EXPECT_EQ(delete_donor(&db, 4), RESULT_OK);
    EXPECT_EQ(db.donors, nullptr);

    Batch history = {};
    history.donor_id = 9;
    ASSERT_NE(append_batch(&db, &history), nullptr);
    int next_id = 0;
    EXPECT_EQ(add_donor(&db, "Next Donor", "1234567890", 0,
                        current_year() - 25, &next_id), RESULT_OK);
    EXPECT_EQ(next_id, 10);

    free_database(&db);
}

TEST(DonorTests, ListDonorsFiltersByGroup)
{
    Database db = {};
    int id = 0;
    ASSERT_EQ(add_donor(&db, "A donor", "9876543210", 0,
                        current_year() - 25, &id), RESULT_OK);
    ASSERT_EQ(add_donor(&db, "B donor", "1234567890", 1,
                        current_year() - 30, &id), RESULT_OK);

    ::testing::internal::CaptureStdout();
    list_donors(&db, 1);
    const std::string filtered = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(filtered.find("B donor"), std::string::npos);
    EXPECT_EQ(filtered.find("A donor"), std::string::npos);
    EXPECT_NE(filtered.find("1 donor(s) shown."), std::string::npos);

    ::testing::internal::CaptureStdout();
    list_donors(&db, -1);
    const std::string all = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(all.find("2 donor(s) shown."), std::string::npos);

    while (db.donors != nullptr) {
        Donor *next = db.donors->next;
        std::free(db.donors);
        db.donors = next;
    }
}
