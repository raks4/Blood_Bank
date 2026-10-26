/* audit.c - append timestamped events to audit.log. */
#include "audit.h"

#include <stdio.h>
#include <time.h>

#define AUDIT_FILE "audit.log"

static int write_field(FILE *file, const char *text)
{
    int ok = 1;

    while ((*text != '\0') && (ok == 1)) {
        char character = *text;

        if ((character == '|') || (character == '\n') || (character == '\r')) {
            character = '_';
        }
        if (fputc((unsigned char)character, file) == EOF) {
            ok = 0;
        }
        text++;
    }
    return ok;
}

int audit_log_event(const char *actor, const char *action)
{
    int ok = 0;
    time_t now = time(NULL);
    struct tm local_time;
    char timestamp[32];

    if ((now != (time_t)-1) && (localtime_r(&now, &local_time) != NULL) &&
        (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &local_time) > 0U)) {
        FILE *file = fopen(AUDIT_FILE, "a");

        if (file != NULL) {
            int written = write_field(file, timestamp);

            if ((written != 0) && (fputs(" | ", file) >= 0)) {
                written = write_field(file, (actor != NULL) ? actor : "anonymous");
            } else {
                written = 0;
            }
            if ((written != 0) && (fputs(" | ", file) >= 0)) {
                written = write_field(file, action);
            } else {
                written = 0;
            }
            if ((written != 0) && (fputc('\n', file) != EOF) && (ferror(file) == 0)) {
                ok = 1;
            }
            if (fclose(file) != 0) {
                ok = 0;
            }
        }
    }
    return ok;
}
