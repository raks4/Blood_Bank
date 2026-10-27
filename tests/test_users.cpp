#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "types.h"
#include "users.h"
}

extern "C" {
extern bool g_fail_next_malloc;
extern size_t g_fail_malloc_size;
}

/* ------------------------------------------------------------------- */
/* Helpers that let us make things fail on purpose                       */
/* ------------------------------------------------------------------- */

/* 2) Random numbers: users.c asks OpenSSL for a random salt.
      This simple fake takes its place, so a test can make that call fail. */
static bool g_random_fails = false;

extern "C" int RAND_bytes(unsigned char *buffer, int count)
{
    if (g_random_fails) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        buffer[i] = static_cast<unsigned char>(i + 1);
    }
    return 1;
}

/* 3) Password hashing: the real one is slow on purpose (600000 rounds).
      This simple fake mixes password and salt, so the tests run instantly,
      and a test can make it fail. Different passwords give different hashes. */
static bool g_hash_fails = false;

extern "C" int PKCS5_PBKDF2_HMAC(const char *password, int password_length,
                                 const unsigned char *salt, int salt_length,
                                 int rounds, const void *algorithm,
                                 int hash_length, unsigned char *hash)
{
    (void)rounds;
    (void)algorithm;
    if (g_hash_fails) {
        return 0;
    }
    for (int i = 0; i < hash_length; i++) {
        hash[i] = static_cast<unsigned char>(salt[i % salt_length] ^
                                             password[i % password_length] ^ i);
    }
    return 1;
}

/* ------------------------------------------------------------------- */
/* Small helper: give back all the memory of the user list              */
/* ------------------------------------------------------------------- */
static void free_users(Database *db)
{
    while (db->users != nullptr) {
        User *next = db->users->next;
        std::free(db->users);
        db->users = next;
    }
}

/* ------------------------------------------------------------------- */
/* One test per function                                                 */
/* ------------------------------------------------------------------- */

/* create_user: makes a new account, or says why it cannot. */
TEST(UserTests, CreateUser)
{
    Database db = {};

    /* Three good accounts. They are added to the end of the list, in order. */
    EXPECT_EQ(create_user(&db, "alice", "AlicePass123"), RESULT_OK);
    EXPECT_EQ(create_user(&db, "bob", "BobPass1234"), RESULT_OK);
    EXPECT_EQ(create_user(&db, "carol", "CarolPass123"), RESULT_OK);
    ASSERT_NE(db.users, nullptr);
    ASSERT_NE(db.users->next, nullptr);
    ASSERT_NE(db.users->next->next, nullptr);
    EXPECT_STREQ(db.users->username, "alice");
    EXPECT_STREQ(db.users->next->username, "bob");
    EXPECT_STREQ(db.users->next->next->username, "carol");
    EXPECT_EQ(db.users->next->next->next, nullptr);

    /* A username that is already taken. */
    EXPECT_EQ(create_user(&db, "alice", "AnotherPass123"), RESULT_EXISTS);

    /* Passwords that are not strong enough. */
    EXPECT_EQ(create_user(&db, "dave", "Short1"), RESULT_BAD_INPUT);          /* under 8 characters */
    EXPECT_EQ(create_user(&db, "dave", "NoDigitsHere"), RESULT_BAD_INPUT);    /* no digit           */
    EXPECT_EQ(create_user(&db, "dave", "12345678"), RESULT_BAD_INPUT);        /* no letter          */
    const std::string too_long(PASSWORD_LEN, 'a');                            /* 64 characters      */
    EXPECT_EQ(create_user(&db, "dave", (too_long + "1").c_str()), RESULT_BAD_INPUT);

    /* A username that does not fit. */
    const std::string long_name(USERNAME_LEN, 'u');
    EXPECT_EQ(create_user(&db, long_name.c_str(), "StrongPass123"), RESULT_BAD_INPUT);

    /* None of the failed attempts added anything. */
    EXPECT_EQ(db.users->next->next->next, nullptr);

    /* The random-number generator breaks. */
    g_random_fails = true;
    EXPECT_EQ(create_user(&db, "erin", "ErinPass123"), RESULT_FAILED);
    g_random_fails = false;

    /* The password hashing breaks. */
    g_hash_fails = true;
    EXPECT_EQ(create_user(&db, "erin", "ErinPass123"), RESULT_FAILED);
    g_hash_fails = false;

    /* The computer runs out of memory. */
    g_fail_malloc_size = sizeof(User);
    g_fail_next_malloc = true;
    EXPECT_EQ(create_user(&db, "frank", "FrankPass123"), RESULT_NO_MEMORY);
    g_fail_next_malloc = false;
    g_fail_malloc_size = 0U;

    EXPECT_EQ(db.users->next->next->next, nullptr);

    EXPECT_EQ(delete_user(&db, "bob"), RESULT_OK);
    ASSERT_NE(db.users, nullptr);
    ASSERT_NE(db.users->next, nullptr);
    EXPECT_STREQ(db.users->username, "alice");
    EXPECT_STREQ(db.users->next->username, "carol");
    EXPECT_EQ(delete_user(&db, "missing"), RESULT_NOT_FOUND);
    EXPECT_EQ(delete_user(&db, nullptr), RESULT_BAD_INPUT);
    EXPECT_EQ(delete_user(&db, "alice"), RESULT_OK);
    EXPECT_STREQ(db.users->username, "carol");
    EXPECT_EQ(delete_user(&db, "carol"), RESULT_OK);
    EXPECT_EQ(db.users, nullptr);

    free_users(&db);
}

/* authenticate_user: returns the account if the name and password match, otherwise NULL. */
TEST(UserTests, AuthenticateUser)
{
    Database db = {};
    ASSERT_EQ(create_user(&db, "alice", "AlicePass123"), RESULT_OK);
    ASSERT_EQ(create_user(&db, "bob", "BobPass1234"), RESULT_OK);

    /* Right name and password: we get that user back. */
    User *alice = authenticate_user(&db, "alice", "AlicePass123");
    ASSERT_NE(alice, nullptr);
    EXPECT_STREQ(alice->username, "alice");
    User *bob = authenticate_user(&db, "bob", "BobPass1234");
    ASSERT_NE(bob, nullptr);
    EXPECT_STREQ(bob->username, "bob");

    /* Wrong password. */
    EXPECT_EQ(authenticate_user(&db, "alice", "WrongPass123"), nullptr);

    /* Bob's password does not open Alice's account. */
    EXPECT_EQ(authenticate_user(&db, "alice", "BobPass1234"), nullptr);

    /* The password hashing breaks: nobody can log in. */
    g_hash_fails = true;
    EXPECT_EQ(authenticate_user(&db, "alice", "AlicePass123"), nullptr);
    g_hash_fails = false;

    /* Unknown username. */
    EXPECT_EQ(authenticate_user(&db, "nobody", "AlicePass123"), nullptr);

    /* Nobody has an account yet. */
    Database empty = {};
    EXPECT_EQ(authenticate_user(&empty, "alice", "AlicePass123"), nullptr);

    free_users(&db);
}

/* change_password: checks the old password, then stores the new one. */
TEST(UserTests, ChangePassword)
{
    Database db = {};
    ASSERT_EQ(create_user(&db, "alice", "OldPass123"), RESULT_OK);
    User *alice = db.users;

    /* Wrong old password. */
    EXPECT_EQ(change_password(alice, "WrongPass123", "NewPass456"), RESULT_BAD_INPUT);

    /* New password too weak. */
    EXPECT_EQ(change_password(alice, "OldPass123", "weak"), RESULT_BAD_INPUT);

    /* New password is the same as the old one. */
    EXPECT_EQ(change_password(alice, "OldPass123", "OldPass123"), RESULT_BAD_INPUT);

    /* The random-number generator breaks. */
    g_random_fails = true;
    EXPECT_EQ(change_password(alice, "OldPass123", "NewPass456"), RESULT_FAILED);
    g_random_fails = false;

    /* None of those attempts changed the password. */
    EXPECT_NE(authenticate_user(&db, "alice", "OldPass123"), nullptr);

    /* A proper change works... */
    EXPECT_EQ(change_password(alice, "OldPass123", "NewPass456"), RESULT_OK);

    /* ...the new password now works and the old one does not. */
    EXPECT_NE(authenticate_user(&db, "alice", "NewPass456"), nullptr);
    EXPECT_EQ(authenticate_user(&db, "alice", "OldPass123"), nullptr);

    free_users(&db);
}
