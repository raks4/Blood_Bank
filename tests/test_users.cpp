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
    ASSERT_EQ(create_user(&db, "existing", "ExistingPass123"), RESULT_OK);
    ASSERT_EQ(create_user(&db, "staff", "StaffPass123"), RESULT_OK);
    ASSERT_NE(db.users->next, nullptr);
    EXPECT_EQ(authenticate_user(&db, "missing", "UnknownPass123"), nullptr);
    EXPECT_STREQ(authenticate_user(&db, "staff", "StaffPass123")->username, "staff");

    EXPECT_EQ(create_user(&db, "weak", "short"), RESULT_BAD_INPUT);
    char long_name[USERNAME_LEN + 1];
    std::memset(long_name, 'u', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    EXPECT_EQ(create_user(&db, long_name, "StrongPass123"), RESULT_BAD_INPUT);
    EXPECT_EQ(create_user(&db, "admin", "StrongPass123"), RESULT_OK);
    EXPECT_EQ(create_user(&db, "admin", "StrongPass123"), RESULT_EXISTS);

    User *admin = authenticate_user(&db, "admin", "StrongPass123");
    ASSERT_NE(admin, nullptr);
    EXPECT_EQ(authenticate_user(&db, "admin", "WrongPass123"), nullptr);
    EXPECT_EQ(change_password(admin, "WrongPass123", "NewStrongPass456"), RESULT_BAD_INPUT);
    EXPECT_EQ(change_password(admin, "StrongPass123", "StrongPass123"), RESULT_BAD_INPUT);
    EXPECT_EQ(change_password(admin, "StrongPass123", "NewStrongPass456"), RESULT_OK);
    EXPECT_EQ(authenticate_user(&db, "admin", "NewStrongPass456"), admin);

    while (db.users != nullptr) {
        User *next = db.users->next;
        std::free(db.users);
        db.users = next;
    }
}
