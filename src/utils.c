/* utils.c - reading input safely, dates, and small helper functions. */
#include "types.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* The eight blood groups. A group is stored as its position in this array. */
static const char *const group_names[GROUP_COUNT] = {
    "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-"
};

/* Becomes 1 when the keyboard input ends (Ctrl+D). Loops check it so they stop. */
static int input_ended = 0;

int input_closed(void)
{
    return input_ended;
}

const char *group_name(int group)
{
    const char *name = "?";

    if ((group >= 0) && (group < GROUP_COUNT)) {
        name = group_names[group];
    }
    return name;
}

static char to_upper(char letter)
{
    if ((letter >= 'a') && (letter <= 'z')) {
        letter = (char)(letter - 'a' + 'A');
    }
    return letter;
}

/* Turns "o-" or "O-" into 7. Returns -1 if the text is not a blood group. */
static int parse_group(const char *text)
{
    int found = -1;
    int group;

    for (group = 0; group < GROUP_COUNT; group++) {
        const char *name = group_names[group];
        int i = 0;

        while ((name[i] != '\0') && (to_upper(text[i]) == name[i])) {
            i++;
        }
        if ((name[i] == '\0') && (text[i] == '\0')) {
            found = group;
        }
    }
    return found;
}

/* Copies text only if it fits (including the final '\0'). Returns 1 if it fitted. */
int copy_text(char *destination, int size, const char *source)
{
    int fits = 0;
    int length = (int)strlen(source);

    if (length < size) {
        (void)memcpy(destination, source, (size_t)length + 1U);
        fits = 1;
    }
    return fits;
}

/* Overwrites a buffer with zeros (used for passwords). */
void wipe(char *buffer, int size)
{
    volatile char *p = buffer;

    for (int i = 0; i < size; i++) {
        p[i] = '\0';
    }
}

/* ---------------------------------------------------------------- */
/* Reading from the keyboard                                         */
/* ---------------------------------------------------------------- */

static void throw_away_rest_of_line(void)
{
    int c = getchar();

    while ((c != '\n') && (c != EOF)) {
        c = getchar();
    }
}

/* Removes spaces at the start and end of the text. */
static void trim(char *text)
{
    int length = (int)strlen(text);
    int start = 0;

    while ((length > 0) && (text[length - 1] == ' ')) {
        length--;
    }
    text[length] = '\0';
    while (text[start] == ' ') {
        start++;
    }
    if (start > 0) {
        for (int i = 0; i <= (length - start); i++) {
            text[i] = text[i + start];
        }
    }
}

/* Shows the prompt and reads one line. Returns 0 only when the input has ended.
   A line that is too long for the buffer is thrown away and returned as empty. */
int read_line(const char *prompt, char *buffer, int size)
{
    int ok = 0;

    printf("%s", prompt);
    (void)fflush(stdout);
    if (fgets(buffer, size, stdin) == NULL) {
        buffer[0] = '\0';
        input_ended = 1;
    } else {
        int length = (int)strlen(buffer);
        if ((length > 0) && (buffer[length - 1] == '\n')) {
            buffer[length - 1] = '\0';
        } else if (feof(stdin) == 0) {
            throw_away_rest_of_line();
            buffer[0] = '\0';
            printf("That was too long.\n");
        } else {
            /* last line of the input had no newline: keep it */
        }
        ok = 1;
    }
    return ok;
}

/* Reads a password without showing it on the screen. */
int read_password(const char *prompt, char *buffer, int size)
{
    struct termios old_settings;
    struct termios hidden_settings;
    int is_terminal = (tcgetattr(STDIN_FILENO, &old_settings) == 0);
    int count = 0;
    int c;
    int ok = 1;

    printf("%s", prompt);
    (void)fflush(stdout);
    if (is_terminal != 0) {
        hidden_settings = old_settings;
        hidden_settings.c_lflag &= (tcflag_t)(~((tcflag_t)ECHO));
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden_settings);
    }
    c = getchar();
    if (c == EOF) {
        ok = 0;
        input_ended = 1;
    }
    while ((c != '\n') && (c != EOF)) {
        if (count < (size - 1)) {
            buffer[count] = (char)c;
            count++;
        }
        c = getchar();
    }
    buffer[count] = '\0';
    if (is_terminal != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_settings);
        printf("\n");
    }
    return ok;
}

/* Converts text like "-25" into a number. Returns 0 if it is not a whole number. */
static int parse_int(const char *text, int *value)
{
    int ok = 0;
    int i = 0;
    int negative = 0;
    long number = 0;

    if (text[0] == '-') {
        negative = 1;
        i = 1;
    }
    if ((text[i] >= '0') && (text[i] <= '9')) {
        ok = 1;
        while ((text[i] >= '0') && (text[i] <= '9') && (ok == 1)) {
            number = (number * 10L) + (long)(text[i] - '0');
            if (number > 1000000000L) {
                ok = 0;
            }
            i++;
        }
        if (text[i] != '\0') {
            ok = 0;
        }
    }
    if (ok == 1) {
        if (negative == 1) {
            number = -number;
        }
        *value = (int)number;
    }
    return ok;
}

/* Allowed in names: letters, digits, spaces and . , - ' (the '|' sign is used
   in the data files, so it must never be accepted). */
static int text_is_valid(const char *text)
{
    int valid = 1;
    int i = 0;

    if (text[0] == '\0') {
        valid = 0;
    }
    while ((text[i] != '\0') && (valid == 1)) {
        char c = text[i];
        int letter_or_digit = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
                              ((c >= '0') && (c <= '9'));
        int allowed_sign = (c == ' ') || (c == '.') || (c == ',') || (c == '-') || (c == '\'');
        int non_english_letter = ((unsigned char)c >= 128U);

        if ((letter_or_digit == 0) && (allowed_sign == 0) && (non_english_letter == 0)) {
            valid = 0;
        }
        i++;
    }
    return valid;
}

/* The ask_... functions keep asking until the answer is valid.
   They return 0 only when the input has ended. */
int ask_text(const char *prompt, char *buffer, int size)
{
    int valid = 0;

    while ((valid == 0) && (input_ended == 0)) {
        if (read_line(prompt, buffer, size) != 0) {
            trim(buffer);
            valid = text_is_valid(buffer);
            if ((valid == 0) && (input_ended == 0)) {
                printf("Please use letters, numbers, spaces and . , - ' only.\n");
            }
        }
    }
    return valid;
}

int ask_phone(const char *prompt, char *buffer, int size)
{
    int valid = 0;

    while ((valid == 0) && (input_ended == 0)) {
        if (read_line(prompt, buffer, size) != 0) {
            int start = (buffer[0] == '+') ? 1 : 0;
            int length = (int)strlen(buffer);
            int i;

            valid = 1;
            if (((length - start) < 7) || ((length - start) > 15)) {
                valid = 0;
            }
            for (i = start; i < length; i++) {
                if ((buffer[i] < '0') || (buffer[i] > '9')) {
                    valid = 0;
                }
            }
            if ((valid == 0) && (input_ended == 0)) {
                printf("A phone number has 7 to 15 digits (it may start with +).\n");
            }
        }
    }
    return valid;
}

int ask_number(const char *prompt, int min, int max, int *value)
{
    int valid = 0;
    int number = 0;
    char line[32];

    while ((valid == 0) && (input_ended == 0)) {
        if (read_line(prompt, line, (int)sizeof(line)) != 0) {
            trim(line);
            if ((parse_int(line, &number) != 0) && (number >= min) && (number <= max)) {
                *value = number;
                valid = 1;
            } else if (input_ended == 0) {
                printf("Please enter a whole number from %d to %d.\n", min, max);
            } else {
                /* input ended: the loop stops */
            }
        }
    }
    return valid;
}

int ask_group(const char *prompt, int *group)
{
    int valid = 0;
    char line[16];

    while ((valid == 0) && (input_ended == 0)) {
        if (read_line(prompt, line, (int)sizeof(line)) != 0) {
            trim(line);
            *group = parse_group(line);
            valid = (*group >= 0) ? 1 : 0;
            if ((valid == 0) && (input_ended == 0)) {
                printf("The blood groups are: A+ A- B+ B- AB+ AB- O+ O-\n");
            }
        }
    }
    return valid;
}

/* Asks a yes/no question. *yes becomes 1 only if the answer starts with y or Y. */
int ask_yes(const char *prompt, int *yes)
{
    char line[16];
    int ok = read_line(prompt, line, (int)sizeof(line));

    *yes = ((ok != 0) && ((line[0] == 'y') || (line[0] == 'Y'))) ? 1 : 0;
    return ok;
}

/* ---------------------------------------------------------------- */
/* Dates. A moment in time is a number of seconds since 1 Jan 1970.  */
/* ---------------------------------------------------------------- */

long today(void)
{
    return (long)time(NULL);
}

int current_year(void)
{
    time_t now = time(NULL);
    struct tm parts;
    int year = 1970;

    if (localtime_r(&now, &parts) != NULL) {
        year = parts.tm_year + 1900;
    }
    return year;
}

/* Writes a date like 2026-06-15 into the buffer (it needs at least 11 characters). */
void format_date(long when, char *buffer, int size)
{
    time_t moment = (time_t)when;
    struct tm parts;

    if ((localtime_r(&moment, &parts) == NULL) ||
        (strftime(buffer, (size_t)size, "%Y-%m-%d", &parts) == 0U)) {
        (void)copy_text(buffer, size, "unknown");
    }
}

/* Turns text like "0aff" into bytes. Used when reading passwords' hashes from a file. */
int hex_to_bytes(const char *hex, unsigned char *bytes, int count)
{
    int ok = ((int)strlen(hex) == (count * 2)) ? 1 : 0;
    int i;

    for (i = 0; (i < count) && (ok == 1); i++) {
        int pair = 0;
        int digit;

        for (digit = 0; digit < 2; digit++) {
            char c = hex[(i * 2) + digit];
            int value = -1;

            if ((c >= '0') && (c <= '9')) {
                value = c - '0';
            } else if ((c >= 'a') && (c <= 'f')) {
                value = (c - 'a') + 10;
            } else {
                ok = 0;
            }
            pair = (pair * 16) + value;
        }
        bytes[i] = (unsigned char)pair;
    }
    return ok;
}

const char *result_text(Result result)
{
    const char *text = "unknown problem";

    switch (result) {
    case RESULT_OK:               text = "done"; break;
    case RESULT_NOT_FOUND:        text = "no record with that ID"; break;
    case RESULT_EXISTS:           text = "that already exists"; break;
    case RESULT_BAD_INPUT:        text = "that value is not allowed"; break;
    case RESULT_WRONG_STATE:      text = "not possible in the current state"; break;
    case RESULT_NOT_ENOUGH_STOCK: text = "not enough blood in stock"; break;
    case RESULT_NO_MEMORY:        text = "out of memory"; break;
    case RESULT_FAILED:           text = "internal error"; break;
    default:                      break;
    }
    return text;
}
