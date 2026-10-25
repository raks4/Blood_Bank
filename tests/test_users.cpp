#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

extern "C" {
#include "types.h"
#include "users.h"
}

TEST(UserTests, AccountCreationLookupAndPasswordChange)
{
    Database db = {};
    User source = {};
    (void)std::strcpy(source.username, "existing");
    ASSERT_NE(append_user(&db, &source), nullptr);

    User second = {};
    (void)std::strcpy(second.username, "staff");
    User *appended = append_user(&db, &second);
    ASSERT_NE(appended, nullptr);
    EXPECT_EQ(db.users->next, appended);
    EXPECT_EQ(find_user(&db, "missing"), nullptr);
    EXPECT_STREQ(find_user(&db, "staff")->username, "staff");

    EXPECT_EQ(create_user(&db, "weak", "short"), RESULT_BAD_INPUT);
    char long_name[USERNAME_LEN + 1];
    std::memset(long_name, 'u', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    EXPECT_EQ(create_user(&db, long_name, "StrongPass123"), RESULT_BAD_INPUT);
    EXPECT_EQ(create_user(&db, "admin", "StrongPass123"), RESULT_OK);
    EXPECT_EQ(create_user(&db, "admin", "StrongPass123"), RESULT_EXISTS);

    User *admin = find_user(&db, "admin");
    ASSERT_NE(admin, nullptr);
    EXPECT_EQ(check_password(admin, "StrongPass123"), 1);
    EXPECT_EQ(check_password(admin, "WrongPass123"), 0);
    EXPECT_EQ(change_password(admin, "WrongPass123", "NewStrongPass456"), RESULT_BAD_INPUT);
    EXPECT_EQ(change_password(admin, "StrongPass123", "StrongPass123"), RESULT_BAD_INPUT);
    EXPECT_EQ(change_password(admin, "StrongPass123", "NewStrongPass456"), RESULT_OK);
    EXPECT_EQ(check_password(admin, "NewStrongPass456"), 1);

    while (db.users != nullptr) {
        User *next = db.users->next;
        std::free(db.users);
        db.users = next;
    }
}
