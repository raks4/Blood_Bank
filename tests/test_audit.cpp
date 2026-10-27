#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

extern "C" {
#include "audit.h"
}

TEST(AuditTests, AppendsTimestampedSanitizedEvents)
{
    static unsigned int run_number = 0U;
    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("bloodbank-audit-test-" + std::to_string(static_cast<long long>(getpid())) + "-" +
         std::to_string(run_number++));
    ASSERT_TRUE(std::filesystem::create_directory(temporary));
    ASSERT_EQ(chdir(temporary.c_str()), 0);

    EXPECT_EQ(audit_log_event(nullptr, nullptr), 1);
    ASSERT_EQ(audit_logger_start(), 1);
    EXPECT_EQ(audit_logger_start(), 1);
    EXPECT_EQ(audit_log_event("staff|one", "donor added (ID 3)"), 1);
    EXPECT_EQ(audit_log_event("staff\none", "logout"), 1);

    std::atomic<int> enqueue_failures{0};
    std::vector<std::thread> writers;
    for (int writer = 0; writer < 4; writer++) {
        writers.emplace_back([writer, &enqueue_failures]() {
            for (int event = 0; event < 25; event++) {
                const std::string action =
                    "parallel event " + std::to_string(writer) + "-" + std::to_string(event);
                if (audit_log_event("worker", action.c_str()) == 0) {
                    enqueue_failures++;
                }
            }
        });
    }
    for (std::thread &writer : writers) {
        writer.join();
    }
    audit_logger_stop();
    EXPECT_EQ(enqueue_failures.load(), 0);

    std::ifstream log("audit.log");
    const std::string contents((std::istreambuf_iterator<char>(log)),
                               std::istreambuf_iterator<char>());
    const std::regex synchronous_entry(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4} \| anonymous \| \n)");
    const std::regex first_entries(
        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4} \| staff_one \| donor added \(ID 3\)\n\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4} \| staff_one \| logout\n)");
    EXPECT_TRUE(std::regex_search(contents, synchronous_entry));
    EXPECT_TRUE(std::regex_search(contents, first_entries));
    EXPECT_EQ(std::count(contents.begin(), contents.end(), '\n'), 103);
    EXPECT_NE(contents.find(" | worker | parallel event "), std::string::npos);

    EXPECT_EQ(chdir(original.c_str()), 0);
    std::filesystem::remove_all(temporary);
}

TEST(AuditTests, ReportsWhenLogCannotBeOpened)
{
    static unsigned int run_number = 0U;
    const std::filesystem::path original = std::filesystem::current_path();
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("bloodbank-audit-error-test-" + std::to_string(static_cast<long long>(getpid())) + "-" +
         std::to_string(run_number++));
    ASSERT_TRUE(std::filesystem::create_directory(temporary));
    ASSERT_EQ(chdir(temporary.c_str()), 0);
    ASSERT_TRUE(std::filesystem::create_directory("audit.log"));

    EXPECT_EQ(audit_logger_start(), 0);
    EXPECT_EQ(audit_log_event("staff", "login successful"), 0);

    std::filesystem::remove_all("audit.log");
    ASSERT_EQ(symlink("/dev/full", "audit.log"), 0);
    ::testing::internal::CaptureStderr();
    ASSERT_EQ(audit_logger_start(), 1);
    EXPECT_EQ(audit_log_event("staff", "write failure"), 1);
    audit_logger_stop();
    const std::string logger_error = ::testing::internal::GetCapturedStderr();
    EXPECT_NE(logger_error.find("WARNING: could not write audit.log"), std::string::npos);

    EXPECT_EQ(chdir(original.c_str()), 0);
    std::filesystem::remove_all(temporary);
}
