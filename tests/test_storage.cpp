#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/resource.h>
#include <unistd.h>

extern "C" {
#include "types.h"
#include "blood.h"
#include "donors.h"
#include "storage.h"
#include "users.h"
}

static Donor *append_test_donor(Database *db, const Donor *source)
{
    Donor *node = static_cast<Donor *>(std::malloc(sizeof(Donor)));
    if (node == nullptr) return nullptr;
    *node = *source;
    node->next = nullptr;
    Donor **link = &db->donors;
    while (*link != nullptr) link = &(*link)->next;
    *link = node;
    return node;
}

static Batch *append_test_batch(Database *db, const Batch *source)
{
    Batch *node = static_cast<Batch *>(std::malloc(sizeof(Batch)));
    if (node == nullptr) return nullptr;
    *node = *source;
    node->next = nullptr;
    Batch **link = &db->batches;
    while (*link != nullptr) link = &(*link)->next;
    *link = node;
    return node;
}

static Request *append_test_request(Database *db, const Request *source)
{
    Request *node = static_cast<Request *>(std::malloc(sizeof(Request)));
    if (node == nullptr) return nullptr;
    *node = *source;
    node->next = nullptr;
    Request **link = &db->requests;
    while (*link != nullptr) link = &(*link)->next;
    *link = node;
    return node;
}

static User *append_test_user(Database *db, const User *source)
{
    User *node = static_cast<User *>(std::malloc(sizeof(User)));
    if (node == nullptr) return nullptr;
    *node = *source;
    node->next = nullptr;
    User **link = &db->users;
    while (*link != nullptr) link = &(*link)->next;
    *link = node;
    return node;
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
    ASSERT_NE(append_test_donor(&db, &donor), nullptr);

    Batch batch = {};
    batch.id = 1;
    batch.donor_id = donor.id;
    batch.group = donor.group;
    batch.ml_collected = 300;
    batch.ml_left = 250;
    batch.collected = 1000L;
    batch.expiry = 2000L;
    batch.status = BATCH_AVAILABLE;
    ASSERT_NE(append_test_batch(&db, &batch), nullptr);

    Request request = {};
    request.id = 1;
    (void)std::strcpy(request.hospital, "City Hospital");
    request.group = 2;
    request.ml = 100;
    request.date = 1500L;
    request.status = REQUEST_PENDING;
    ASSERT_NE(append_test_request(&db, &request), nullptr);

    User user = {};
    (void)std::strcpy(user.username, "staff");
    user.salt[0] = 10U;
    user.hash[0] = 20U;
    ASSERT_NE(append_test_user(&db, &user), nullptr);
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

TEST(StorageTests, ValidatesFileFormatsAndReportsSaveFailures)
{
    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("bloodbank-storage-errors-" + std::to_string(static_cast<long long>(getpid())));
    ASSERT_TRUE(std::filesystem::create_directory(temporary));
    std::filesystem::current_path(temporary);

    {
        std::ofstream donors("donors.txt");
        ASSERT_TRUE(donors.good());
        donors << "\n \t\n"
                  "1|Old Active|1234567890|0|1990|1|0\r\n"
                  "2|Old Inactive|1234567890|1|1990|0|0\n";
    }
    Database legacy = {};
    ASSERT_EQ(load_all(&legacy), 1);
    ASSERT_NE(legacy.donors, nullptr);
    EXPECT_NE(legacy.donors->next, nullptr);
    free_database(&legacy);

    {
        std::ofstream donors("donors.txt", std::ios::trunc);
        ASSERT_TRUE(donors.good());
        donors << "0|Bad Donor|1234567890|0|1990|1|0|1|0\n";
    }
    Database bad_donor = {};
    EXPECT_EQ(load_all(&bad_donor), 0);
    free_database(&bad_donor);
    std::filesystem::remove("donors.txt");

    {
        std::ofstream batches("batches.txt");
        ASSERT_TRUE(batches.good());
        batches << "1|0|0|300|301|100|200|1\n";
    }
    Database bad_batch = {};
    EXPECT_EQ(load_all(&bad_batch), 0);
    free_database(&bad_batch);
    std::filesystem::remove("batches.txt");

    {
        std::ofstream requests("requests.txt");
        ASSERT_TRUE(requests.good());
        requests << "1|City Hospital|8|100|100|0\n";
    }
    Database bad_request = {};
    EXPECT_EQ(load_all(&bad_request), 0);
    free_database(&bad_request);
    std::filesystem::remove("requests.txt");

    {
        std::ofstream users("users.txt");
        ASSERT_TRUE(users.good());
        users << "staff|00|00\n";
    }
    Database bad_user = {};
    EXPECT_EQ(load_all(&bad_user), 0);
    free_database(&bad_user);
    std::filesystem::remove("users.txt");

    {
        std::ofstream batches("batches.txt");
        ASSERT_TRUE(batches.good());
        batches << "malformed batch\n";
    }
    Database malformed_batch = {};
    EXPECT_EQ(load_all(&malformed_batch), 0);
    free_database(&malformed_batch);
    std::filesystem::remove("batches.txt");

    {
        std::ofstream requests("requests.txt");
        ASSERT_TRUE(requests.good());
        requests << "malformed request\n";
    }
    Database malformed_request = {};
    EXPECT_EQ(load_all(&malformed_request), 0);
    free_database(&malformed_request);
    std::filesystem::remove("requests.txt");

    for (const char *name : {"donors.tmp", "batches.tmp", "requests.tmp", "users.tmp"}) {
        ASSERT_TRUE(std::filesystem::create_directory(name));
    }
    Database empty = {};
    EXPECT_EQ(save_all(&empty), 0);
    for (const char *name : {"donors.tmp", "batches.tmp", "requests.tmp", "users.tmp"}) {
        std::filesystem::remove(name);
    }

    struct rlimit original_limit = {};
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &original_limit), 0);
    {
        std::ofstream donors("donors.txt");
        ASSERT_TRUE(donors.good());
        donors << "1|A Donor|1234567890|0|1990|1|0|1|0\n";
    }
    struct rlimit restricted_limit = original_limit;
    restricted_limit.rlim_cur = 0;
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &restricted_limit), 0);
    Database cannot_open = {};
    EXPECT_EQ(load_all(&cannot_open), 0);
    free_database(&cannot_open);
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &original_limit), 0);
    std::filesystem::remove("donors.txt");

    ASSERT_TRUE(std::filesystem::create_directory("donors.txt"));
    EXPECT_EQ(save_all(&empty), 0);

    std::filesystem::current_path(original);
    std::filesystem::remove_all(temporary);
}
