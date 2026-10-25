#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

extern "C" {
#include "types.h"
#include "utils.h"
}

TEST(UtilsTests, TextGroupDateHexAndResultHelpers)
{
    EXPECT_STREQ(group_name(0), "A+");
    EXPECT_STREQ(group_name(GROUP_COUNT - 1), "O-");
    EXPECT_STREQ(group_name(-1), "?");

    char text[8] = {};
    EXPECT_EQ(copy_text(text, static_cast<int>(sizeof(text)), "donor"), 1);
    EXPECT_STREQ(text, "donor");
    EXPECT_EQ(copy_text(text, static_cast<int>(sizeof(text)), "too long text"), 0);
    wipe(text, static_cast<int>(sizeof(text)));
    EXPECT_EQ(text[0], '\0');

    EXPECT_GT(today(), 0L);
    EXPECT_GE(current_year(), 1970);
    char date[16];
    format_date(0L, date, static_cast<int>(sizeof(date)));
    EXPECT_EQ(std::strlen(date), 10U);

    unsigned char bytes[2] = {};
    EXPECT_EQ(hex_to_bytes("0aff", bytes, 2), 1);
    EXPECT_EQ(bytes[0], 0x0aU);
    EXPECT_EQ(bytes[1], 0xffU);
    EXPECT_EQ(hex_to_bytes("0agf", bytes, 2), 0);

    EXPECT_STREQ(result_text(RESULT_OK), "done");
    EXPECT_STREQ(result_text(static_cast<Result>(99)), "unknown problem");
}

TEST(UtilsTests, KeyboardReadersValidateAndReturnInput)
{
    char input_path[] = "/tmp/bloodbank-utils-XXXXXX";
    const int input_fd = mkstemp(input_path);
    ASSERT_GE(input_fd, 0);
    const int saved_stdin = dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);

    const char input_data[] =
        "Bad|Name\n"
        "Ada Lovelace\n"
        "123-4567\n"
        "+12345678901\n"
        "2x\n"
        "-3\n"
        "invalid\n"
        "ab-\n"
        "yes\n"
        "secret123\n";
    ASSERT_EQ(write(input_fd, input_data, sizeof(input_data) - 1U),
              static_cast<ssize_t>(sizeof(input_data) - 1U));
    ASSERT_EQ(lseek(input_fd, 0, SEEK_SET), 0);
    ASSERT_EQ(dup2(input_fd, STDIN_FILENO), STDIN_FILENO);
    (void)close(input_fd);
    clearerr(stdin);
    ASSERT_EQ(setvbuf(stdin, nullptr, _IONBF, 0), 0);
    EXPECT_EQ(input_closed(), 0);

    char text[NAME_LEN];
    EXPECT_EQ(ask_text("", text, static_cast<int>(sizeof(text))), 1);
    EXPECT_STREQ(text, "Ada Lovelace");

    char phone[PHONE_LEN];
    EXPECT_EQ(ask_phone("", phone, static_cast<int>(sizeof(phone))), 1);
    EXPECT_STREQ(phone, "+12345678901");

    int number = 0;
    EXPECT_EQ(ask_number("", -5, 5, &number), 1);
    EXPECT_EQ(number, -3);

    int group = -1;
    EXPECT_EQ(ask_group("", &group), 1);
    EXPECT_EQ(group, 5);

    int yes = 0;
    EXPECT_EQ(ask_yes("", &yes), 1);
    EXPECT_EQ(yes, 1);

    char password[8];
    EXPECT_EQ(read_password("", password, static_cast<int>(sizeof(password))), 1);
    EXPECT_STREQ(password, "secret1");

    EXPECT_EQ(dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    (void)close(saved_stdin);
    clearerr(stdin);
    (void)unlink(input_path);
}
