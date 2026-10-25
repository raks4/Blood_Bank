#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

extern "C" {
#include "types.h"
#include "blood.h"
#include "donors.h"
#include "storage.h"
#include "users.h"
}

TEST(StorageTests, SavesLoadsAndRejectsDamagedData)
{
    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("bloodbank-storage-test-" + std::to_string(static_cast<long long>(getpid())));
    ASSERT_TRUE(std::filesystem::create_directory(temporary));
    std::filesystem::current_path(temporary);

    Database empty = {};
    EXPECT_EQ(load_all(&empty), 1);

    Database db = {};
    Donor donor = {};
    donor.id = 1;
    (void)std::strcpy(donor.name, "Avery Donor");
    (void)std::strcpy(donor.phone, "9876543210");
    donor.group = 2;
    donor.birth_year = 1990;
    donor.active = 1;
    ASSERT_NE(append_donor(&db, &donor), nullptr);

    Batch batch = {};
    batch.id = 1;
    batch.donor_id = donor.id;
    batch.group = donor.group;
    batch.ml_collected = 300;
    batch.ml_left = 250;
    batch.collected = 1000L;
    batch.expiry = 2000L;
    batch.status = BATCH_AVAILABLE;
    ASSERT_NE(append_batch(&db, &batch), nullptr);

    Request request = {};
    request.id = 1;
    (void)std::strcpy(request.hospital, "City Hospital");
    request.group = 2;
    request.ml = 100;
    request.date = 1500L;
    request.status = REQUEST_PENDING;
    ASSERT_NE(append_request(&db, &request), nullptr);

    User user = {};
    (void)std::strcpy(user.username, "staff");
    user.salt[0] = 10U;
    user.hash[0] = 20U;
    ASSERT_NE(append_user(&db, &user), nullptr);
    ASSERT_EQ(save_all(&db), 1);

    Database loaded = {};
    ASSERT_EQ(load_all(&loaded), 1);
    ASSERT_NE(loaded.donors, nullptr);
    EXPECT_STREQ(loaded.donors->name, "Avery Donor");
    ASSERT_NE(loaded.batches, nullptr);
    EXPECT_EQ(loaded.batches->ml_left, 250);
    ASSERT_NE(loaded.requests, nullptr);
    EXPECT_STREQ(loaded.requests->hospital, "City Hospital");
    ASSERT_NE(loaded.users, nullptr);
    EXPECT_STREQ(loaded.users->username, "staff");
    EXPECT_EQ(loaded.users->salt[0], 10U);
    EXPECT_EQ(loaded.users->hash[0], 20U);
    free_database(&loaded);
    free_database(&db);

    {
        std::ofstream old_format("donors.txt", std::ios::trunc);
        ASSERT_TRUE(old_format.good());
        old_format << "2|Old Active|1234567890|0|1990|1|0\n"
                      "3|Old Inactive|1234567890|1|1990|0|0\n";
    }
    Database migrated = {};
    ASSERT_EQ(load_all(&migrated), 1);
    ASSERT_NE(migrated.donors, nullptr);
    EXPECT_GT(migrated.donors->registered_at, 0L);
    EXPECT_EQ(migrated.donors->deactivated_at, 0L);
    ASSERT_NE(migrated.donors->next, nullptr);
    EXPECT_GT(migrated.donors->next->deactivated_at, 0L);
    free_database(&migrated);

    {
        std::ofstream damaged("donors.txt", std::ios::trunc);
        ASSERT_TRUE(damaged.good());
        damaged << "invalid record\n";
    }
    Database bad = {};
    EXPECT_EQ(load_all(&bad), 0);
    free_database(&bad);

    std::filesystem::current_path(original);
    std::filesystem::remove_all(temporary);
}
