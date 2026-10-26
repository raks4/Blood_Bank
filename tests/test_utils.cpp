#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

extern "C" {
#include "types.h"
#include "utils.h"
}

/* ------------------------------------------------------------------- */
/* Helpers                                                               */
/* ------------------------------------------------------------------- */

/* Every test gets its own "keyboard": we put the text into a file and make it
   the standard input. After the test, the real standard input is put back. */
class UtilsTests : public ::testing::Test {
protected:
    void SetUp() override
    {
        saved_stdin_ = dup(STDIN_FILENO);
    }

    void TearDown() override
    {
        (void)dup2(saved_stdin_, STDIN_FILENO);
        (void)close(saved_stdin_);
        clearerr(stdin);
    }

    /* "Types" this text on the keyboard. */
    static void type(const std::string &text)
    {
        char path[] = "/tmp/bloodbank-utils-XXXXXX";
        const int fd = mkstemp(path);
        (void)close(fd);
        {
            std::ofstream file(path);
            file << text;
        }
        (void)std::freopen(path, "r", stdin);
        (void)setvbuf(stdin, nullptr, _IONBF, 0);
        (void)unlink(path);
    }

    int saved_stdin_ = -1;
};

/* ------------------------------------------------------------------- */
/* One test per function                                                 */
/* ------------------------------------------------------------------- */

/* today: the current time in seconds. */
TEST_F(UtilsTests, Today)
{
    const long before = static_cast<long>(std::time(nullptr));
    const long now = today();
    const long after = static_cast<long>(std::time(nullptr));

    EXPECT_GE(now, before);
    EXPECT_LE(now, after);
}

/* current_year: the year of the current time. */
TEST_F(UtilsTests, CurrentYear)
{
    const std::time_t now = std::time(nullptr);
    struct tm parts;
    ASSERT_NE(localtime_r(&now, &parts), nullptr);

    EXPECT_EQ(current_year(), parts.tm_year + 1900);
}


/* ask_input: keeps asking until the answer is valid. */
TEST_F(UtilsTests, AskInput)
{
    /* ---- Text: letters, digits, spaces and . , - ' are fine; '|' and empty are not ---- */
    type("Bad|Name\n"
         "\n"
         "   Ada Lovelace   \n");                 /* spaces around the name are removed */
    char text[NAME_LEN];
    ::testing::internal::CaptureStdout();
    EXPECT_EQ(ask_input("", INPUT_TEXT, text, static_cast<int>(sizeof(text)), nullptr, 0, 0), 1);
    std::string shown = ::testing::internal::GetCapturedStdout();
    EXPECT_STREQ(text, "Ada Lovelace");
    EXPECT_NE(shown.find("Please use letters, numbers, spaces"), std::string::npos);

    type("O'Brien-Smith, Jr.\n");                 /* the allowed signs */
    EXPECT_EQ(ask_input("", INPUT_TEXT, text, static_cast<int>(sizeof(text)), nullptr, 0, 0), 1);
    EXPECT_STREQ(text, "O'Brien-Smith, Jr.");

    type("Jos\xc3\xa9\n");                        /* a letter that is not English (UTF-8) */
    EXPECT_EQ(ask_input("", INPUT_TEXT, text, static_cast<int>(sizeof(text)), nullptr, 0, 0), 1);
    EXPECT_STREQ(text, "Jos\xc3\xa9");

    /* ---- Phone: 7 to 15 digits, may start with + ---- */
    type("123\n"                                  /* too short                  */
         "123-4567\n"                             /* not only digits            */
         "1234567890123456\n"                     /* too long (16 digits)       */
         "+12345678901\n");                       /* fine                       */
    char phone[PHONE_LEN];
    ::testing::internal::CaptureStdout();
    EXPECT_EQ(ask_input("", INPUT_PHONE, phone, static_cast<int>(sizeof(phone)), nullptr, 0, 0), 1);
    shown = ::testing::internal::GetCapturedStdout();
    EXPECT_STREQ(phone, "+12345678901");
    EXPECT_NE(shown.find("A phone number has 7 to 15 digits"), std::string::npos);

    type("1234567\n");                            /* exactly 7 digits is fine   */
    EXPECT_EQ(ask_input("", INPUT_PHONE, phone, static_cast<int>(sizeof(phone)), nullptr, 0, 0), 1);
    EXPECT_STREQ(phone, "1234567");

    /* ---- Number: a whole number inside min..max ---- */
    type("2x\n"                                   /* letters after the digits   */
         "invalid\n"                              /* not a number               */
         "-\n"                                    /* only a minus sign          */
         "9\n"                                    /* above the maximum          */
         "99999999999\n"                          /* far too big                */
         "1234567890123456789012345678901234567890\n" /* longer than the input buffer */
         "  -3  \n");                             /* fine (spaces are removed)  */
    int number = 0;
    ::testing::internal::CaptureStdout();
    EXPECT_EQ(ask_input("", INPUT_NUMBER, nullptr, 0, &number, -5, 5), 1);
    shown = ::testing::internal::GetCapturedStdout();
    EXPECT_EQ(number, -3);
    EXPECT_NE(shown.find("Please enter a whole number from -5 to 5."), std::string::npos);
    EXPECT_NE(shown.find("That was too long."), std::string::npos);

    /* ---- Blood group: every name works, in any letter case ---- */
    const char *const names[] = {"A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-"};
    for (int expected = 0; expected < 8; expected++) {
        type(std::string(names[expected]) + "\n");
        int group = -1;
        EXPECT_EQ(ask_input("", INPUT_GROUP, nullptr, 0, &group, 0, 0), 1);
        EXPECT_EQ(group, expected);
    }
    type("o-\n");
    int group = -1;
    EXPECT_EQ(ask_input("", INPUT_GROUP, nullptr, 0, &group, 0, 0), 1);
    EXPECT_EQ(group, 7);

    type("xyz\n"                                  /* not a blood group          */
         "A\n"                                    /* only the start of one      */
         "A++\n"                                  /* too much                   */
         "ab-\n");                                /* fine                       */
    ::testing::internal::CaptureStdout();
    EXPECT_EQ(ask_input("", INPUT_GROUP, nullptr, 0, &group, 0, 0), 1);
    shown = ::testing::internal::GetCapturedStdout();
    EXPECT_EQ(group, 5);
    EXPECT_NE(shown.find("The blood groups are: A+ A- B+ B- AB+ AB- O+ O-"), std::string::npos);

    /* ---- Yes/no: yes only when the answer starts with y or Y ---- */
    int yes = -1;
    type("yes\n");
    EXPECT_EQ(ask_input("", INPUT_YES, nullptr, 0, &yes, 0, 0), 1);
    EXPECT_EQ(yes, 1);
    type("Y\n");
    EXPECT_EQ(ask_input("", INPUT_YES, nullptr, 0, &yes, 0, 0), 1);
    EXPECT_EQ(yes, 1);
    type("no\n");
    EXPECT_EQ(ask_input("", INPUT_YES, nullptr, 0, &yes, 0, 0), 1);
    EXPECT_EQ(yes, 0);

    /* ---- An unknown kind of question: nothing is asked, the answer is 0 ---- */
    type("anything\n");
    EXPECT_EQ(ask_input("", static_cast<InputKind>(99), text, static_cast<int>(sizeof(text)),
                        nullptr, 0, 0), 0);
}

/* read_input: reads one line from the keyboard. */
TEST_F(UtilsTests, ReadInput)
{
    char line[8];

    /* A normal line: the prompt is shown and the line is returned without the Enter. */
    type("hello\n");
    ::testing::internal::CaptureStdout();
    EXPECT_EQ(read_input("Name: ", line, static_cast<int>(sizeof(line)), 0), 1);
    EXPECT_EQ(::testing::internal::GetCapturedStdout(), "Name: ");
    EXPECT_STREQ(line, "hello");

    /* The last line may have no Enter at the end. */
    type("abc");
    EXPECT_EQ(read_input("", line, static_cast<int>(sizeof(line)), 0), 1);
    EXPECT_STREQ(line, "abc");

    /* A line that does not fit: it is refused (empty answer) and the rest is thrown away,
       so the next read starts with the next line. */
    type("this input line is too long\n"
         "Valid\n");
    ::testing::internal::CaptureStdout();
    EXPECT_EQ(read_input("", line, static_cast<int>(sizeof(line)), 0), 1);
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("That was too long."),
              std::string::npos);
    EXPECT_STREQ(line, "");
    EXPECT_EQ(read_input("", line, static_cast<int>(sizeof(line)), 0), 1);
    EXPECT_STREQ(line, "Valid");

    /* Hidden input (a password) from a file: a long password is cut to fit. */
    type("secret123\n");
    EXPECT_EQ(read_input("", line, static_cast<int>(sizeof(line)), 1), 1);
    EXPECT_STREQ(line, "secret1");

    /* Hidden input from a real terminal: typing is not shown while the password is
       read, and the terminal is put back to normal afterwards.
       (We make a pretend terminal; a helper thread "types" once the echo is switched off.) */
    {
        const int master = posix_openpt(O_RDWR | O_NOCTTY);
        ASSERT_GE(master, 0);
        ASSERT_EQ(grantpt(master), 0);
        ASSERT_EQ(unlockpt(master), 0);
        const int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
        ASSERT_GE(slave, 0);
        ASSERT_GE(dup2(slave, STDIN_FILENO), 0);
        (void)close(slave);
        clearerr(stdin);
        (void)setvbuf(stdin, nullptr, _IONBF, 0);

        std::thread typist([master]() {
            struct termios settings;
            for (int i = 0; i < 3000; i++) {            /* wait until the echo is off */
                if ((tcgetattr(master, &settings) == 0) && ((settings.c_lflag & ECHO) == 0)) {
                    break;
                }
                usleep(1000);
            }
            const char password[] = "secret123\n";
            (void)write(master, password, sizeof(password) - 1U);
        });

        char password[16];
        ::testing::internal::CaptureStdout();
        EXPECT_EQ(read_input("Password: ", password, static_cast<int>(sizeof(password)), 1), 1);
        const std::string shown = ::testing::internal::GetCapturedStdout();
        typist.join();

        EXPECT_STREQ(password, "secret123");
        EXPECT_EQ(shown, "Password: \n");                /* the typing itself was not printed */
        struct termios after;
        ASSERT_EQ(tcgetattr(STDIN_FILENO, &after), 0);
        EXPECT_NE(after.c_lflag & ECHO, 0U);              /* echo is back on */
        (void)close(master);
    }

    /* The input ends (Ctrl+D): the answer is 0 and the line is empty. */
    type("");
    EXPECT_EQ(read_input("", line, static_cast<int>(sizeof(line)), 0), 0);
    EXPECT_STREQ(line, "");

    type("");
    EXPECT_EQ(read_input("", line, static_cast<int>(sizeof(line)), 1), 0);
    EXPECT_STREQ(line, "");

    /* ask_input gives up when the current input stream has ended. */
    char text[NAME_LEN];
    EXPECT_EQ(ask_input("", INPUT_TEXT, text, static_cast<int>(sizeof(text)), nullptr, 0, 0), 0);
}
