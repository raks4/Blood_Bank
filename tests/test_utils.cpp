#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

extern "C" {
#include "types.h"
#include "utils.h"
}

extern "C" int bloodbank_app_main(void);

TEST(UtilsTests, ClockHelpers)
{
    EXPECT_GT(today(), 0L);
    EXPECT_GE(current_year(), 1970);
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
        "   Ada Lovelace   \n"
        "123\n"
        "123-4567\n"
        "+12345678901\n"
        "2x\n"
        "-3\n"
        "invalid\n"
        "ab-\n"
        "yes\n"
        "secret123\n"
        "this input line is too long\n"
        "Valid\n";
    ASSERT_EQ(write(input_fd, input_data, sizeof(input_data) - 1U),
              static_cast<ssize_t>(sizeof(input_data) - 1U));
    ASSERT_EQ(lseek(input_fd, 0, SEEK_SET), 0);
    ASSERT_EQ(dup2(input_fd, STDIN_FILENO), STDIN_FILENO);
    (void)close(input_fd);
    clearerr(stdin);
    ASSERT_EQ(setvbuf(stdin, nullptr, _IONBF, 0), 0);
    char text[NAME_LEN];
    EXPECT_EQ(ask_input("", INPUT_TEXT, text, static_cast<int>(sizeof(text)),
                        nullptr, 0, 0), 1);
    EXPECT_STREQ(text, "Ada Lovelace");

    char phone[PHONE_LEN];
    EXPECT_EQ(ask_input("", INPUT_PHONE, phone, static_cast<int>(sizeof(phone)),
                        nullptr, 0, 0), 1);
    EXPECT_STREQ(phone, "+12345678901");

    int number = 0;
    EXPECT_EQ(ask_input("", INPUT_NUMBER, nullptr, 0, &number, -5, 5), 1);
    EXPECT_EQ(number, -3);

    int group = -1;
    EXPECT_EQ(ask_input("", INPUT_GROUP, nullptr, 0, &group, 0, 0), 1);
    EXPECT_EQ(group, 5);

    int yes = 0;
    EXPECT_EQ(ask_input("", INPUT_YES, nullptr, 0, &yes, 0, 0), 1);
    EXPECT_EQ(yes, 1);

    char password[8];
    EXPECT_EQ(read_input("", password, static_cast<int>(sizeof(password)), 1), 1);
    EXPECT_STREQ(password, "secret1");

    char short_buffer[8];
    EXPECT_EQ(read_input("", short_buffer, static_cast<int>(sizeof(short_buffer)), 0), 1);
    EXPECT_STREQ(short_buffer, "");
    EXPECT_EQ(ask_input("", INPUT_TEXT, text, static_cast<int>(sizeof(text)),
                        nullptr, 0, 0), 1);
    EXPECT_STREQ(text, "Valid");

    EXPECT_EQ(dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    (void)close(saved_stdin);
    clearerr(stdin);
    (void)unlink(input_path);
}

TEST(UtilsTests, ZzzAppExitsWhenMenuInputEnds)
{
    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("bloodbank-eof-test-" + std::to_string(static_cast<long long>(getpid())));
    ASSERT_TRUE(std::filesystem::create_directory(temporary));
    std::filesystem::current_path(temporary);
    {
        std::ofstream input_file("input.txt");
        ASSERT_TRUE(input_file.good());
        input_file << "admin\n"
                      "StrongPass123\n"
                      "StrongPass123\n"
                      "admin\n"
                      "StrongPass123\n";
    }

    const int saved_stdin = dup(STDIN_FILENO);
    ASSERT_GE(saved_stdin, 0);
    (void)std::freopen("input.txt", "r", stdin);
    (void)setvbuf(stdin, nullptr, _IONBF, 0);
    ::testing::internal::CaptureStdout();
    const int exit_code = bloodbank_app_main();
    const std::string output = ::testing::internal::GetCapturedStdout();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("Goodbye!"), std::string::npos);

    EXPECT_EQ(dup2(saved_stdin, STDIN_FILENO), STDIN_FILENO);
    (void)close(saved_stdin);
    clearerr(stdin);
    std::filesystem::current_path(original);
    std::filesystem::remove_all(temporary);
}
