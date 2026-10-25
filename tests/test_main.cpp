#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

extern "C" {
#include "types.h"
}

extern "C" int bloodbank_app_main(void);

struct AppResult {
    int exit_code;
    std::string output;
};

static AppResult run_app(const std::string &input)
{
    static unsigned int run_number = 0U;
    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("bloodbank-main-test-" + std::to_string(static_cast<long long>(getpid())) + "-" +
         std::to_string(run_number++));
    std::filesystem::create_directory(temporary);
    std::filesystem::current_path(temporary);

    const std::filesystem::path input_path = temporary / "input.txt";
    {
        std::ofstream input_file(input_path);
        input_file << input;
    }

    const int saved_stdin = dup(STDIN_FILENO);
    (void)std::freopen(input_path.string().c_str(), "r", stdin);
    (void)setvbuf(stdin, nullptr, _IONBF, 0);
    ::testing::internal::CaptureStdout();
    const int exit_code = bloodbank_app_main();
    const std::string output = ::testing::internal::GetCapturedStdout();

    (void)dup2(saved_stdin, STDIN_FILENO);
    (void)close(saved_stdin);
    clearerr(stdin);
    std::filesystem::current_path(original);
    std::filesystem::remove_all(temporary);
    return {exit_code, output};
}

static const std::string setup_and_login =
    "admin\n"
    "StrongPass123\n"
    "StrongPass123\n"
    "admin\n"
    "StrongPass123\n";

TEST(MainTests, CreatesAccountLogsInRunsDonorMenuAndExits)
{
    const AppResult result = run_app(
        setup_and_login +
        "1\n"
        "1\n"
        "Alice Donor\n"
        "9876543210\n"
        "A+\n"
        "1995\n"
        "2\n"
        "5\n"
        "1\n"
        "y\n"
        "2\n"
        "0\n"
        "0\n");

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("Welcome, admin!"), std::string::npos);
    EXPECT_NE(result.output.find("Donor added."), std::string::npos);
    EXPECT_NE(result.output.find("Donor 1 was deleted."), std::string::npos);
    EXPECT_NE(result.output.find("0 donor(s) shown."), std::string::npos);
    EXPECT_NE(result.output.find("Goodbye!"), std::string::npos);
}

TEST(MainTests, LoginStopsAfterThreeInvalidAttempts)
{
    const AppResult result = run_app(
        "admin\n"
        "StrongPass123\n"
        "StrongPass123\n"
        "wrong\n"
        "badpass\n"
        "wrong\n"
        "badpass\n"
        "wrong\n"
        "badpass\n");

    EXPECT_EQ(result.exit_code, 1);
    EXPECT_EQ(result.output.find("Welcome, wrong!"), std::string::npos);
    const std::string failure = "Wrong username or password.";
    size_t position = 0U;
    int attempts = 0;
    while ((position = result.output.find(failure, position)) != std::string::npos) {
        ++attempts;
        position += failure.length();
    }
    EXPECT_EQ(attempts, MAX_LOGIN_ATTEMPTS);
}

TEST(MainTests, DonationMenuReportsMissingDonorsAndListsHistory)
{
    const AppResult result = run_app(
        setup_and_login +
        "2\n"
        "1\n"
        "99\n"
        "2\n"
        "99\n"
        "3\n"
        "0\n"
        "0\n");

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("There is no donor with ID 99."), std::string::npos);
    EXPECT_NE(result.output.find("0 batch(es) shown."), std::string::npos);
}

TEST(MainTests, InventoryMenuTestsAndDiscardsDonationBatches)
{
    const AppResult result = run_app(
        setup_and_login +
        "1\n"
        "1\n"
        "Alice Donor\n"
        "9876543210\n"
        "A+\n"
        "1995\n"
        "0\n"
        "2\n"
        "1\n"
        "1\n"
        "300\n"
        "0\n"
        "3\n"
        "2\n"
        "A+\n"
        "3\n"
        "4\n"
        "1\n"
        "n\n"
        "4\n"
        "1\n"
        "y\n"
        "5\n"
        "1\n"
        "n\n"
        "5\n"
        "1\n"
        "y\n"
        "0\n"
        "0\n");

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("Cancelled. Use 'Discard a batch'"), std::string::npos);
    EXPECT_NE(result.output.find("Batch 1 can now be used."), std::string::npos);
    EXPECT_NE(result.output.find("Cancelled."), std::string::npos);
    EXPECT_NE(result.output.find("Batch 1 was discarded."), std::string::npos);
}

TEST(MainTests, RequestsAndAccountMenusHandleSuccessfulActions)
{
    const AppResult result = run_app(
        setup_and_login +
        "4\n"
        "1\n"
        "City Hospital\n"
        "A+\n"
        "100\n"
        "2\n"
        "1\n"
        "3\n"
        "1\n"
        "0\n"
        "5\n"
        "1\n"
        "staff\n"
        "StaffPass123\n"
        "StaffPass123\n"
        "2\n"
        "StrongPass123\n"
        "NewStrongPass456\n"
        "NewStrongPass456\n"
        "0\n"
        "0\n");

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("Request 1 saved."), std::string::npos);
    EXPECT_NE(result.output.find("not enough blood in stock"), std::string::npos);
    EXPECT_NE(result.output.find("Request 1 rejected."), std::string::npos);
    EXPECT_NE(result.output.find("User staff created."), std::string::npos);
    EXPECT_NE(result.output.find("Password changed."), std::string::npos);
}
