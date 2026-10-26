#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

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

    EXPECT_EQ(audit_log_event("staff|one", "donor added (ID 3)"), 1);
    EXPECT_EQ(audit_log_event("staff\none", "logout"), 1);

    std::ifstream log("audit.log");
    const std::string contents((std::istreambuf_iterator<char>(log)),
                               std::istreambuf_iterator<char>());
    const std::regex entry(
        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4} \| staff_one \| donor added \(ID 3\)\n\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4} \| staff_one \| logout\n)");
    EXPECT_TRUE(std::regex_match(contents, entry));

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

    EXPECT_EQ(audit_log_event("staff", "login successful"), 0);

    EXPECT_EQ(chdir(original.c_str()), 0);
    std::filesystem::remove_all(temporary);
}
