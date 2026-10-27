/* audit.c - append timestamped events to audit.log. */
#include "audit.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define AUDIT_FILE "audit.log"
#define AUDIT_QUEUE_CAPACITY 256U
#define AUDIT_ACTOR_SIZE 64U
#define AUDIT_ACTION_SIZE 128U

typedef struct {
    char actor[AUDIT_ACTOR_SIZE];
    char action[AUDIT_ACTION_SIZE];
} AuditMessage;

static pthread_mutex_t audit_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t audit_condition = PTHREAD_COND_INITIALIZER;
static pthread_cond_t audit_space_condition = PTHREAD_COND_INITIALIZER;
static pthread_t audit_thread;
static AuditMessage audit_queue[AUDIT_QUEUE_CAPACITY];
static FILE *audit_file;
static size_t queue_head;
static size_t queue_count;
static int logger_active;
static int logger_stopping;
static int logger_started;
static int logger_failed;

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

static int write_event(FILE *file, const char *actor, const char *action)
{
    int ok = 0;
    time_t now = time(NULL);
    struct tm local_time;
    char timestamp[32];

    if ((now != (time_t)-1) && (localtime_r(&now, &local_time) != NULL) &&
        (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &local_time) > 0U)) {
        int written = write_field(file, timestamp);

        if ((written != 0) && (fputs(" | ", file) >= 0)) {
            written = write_field(file, (actor != NULL) ? actor : "anonymous");
        } else {
            written = 0;
        }
        if ((written != 0) && (fputs(" | ", file) >= 0)) {
            written = write_field(file, (action != NULL) ? action : "");
        } else {
            written = 0;
        }
        if ((written != 0) && (fputc('\n', file) != EOF) && (fflush(file) == 0) &&
            (ferror(file) == 0)) {
            ok = 1;
        }
    }
    return ok;
}

static void *audit_worker(void *unused)
{
    int done = 0;

    (void)unused;
    while (done == 0) {
        AuditMessage message;

        (void)pthread_mutex_lock(&audit_mutex);
        while ((queue_count == 0U) && (logger_stopping == 0)) {
            (void)pthread_cond_wait(&audit_condition, &audit_mutex);
        }
        if ((queue_count == 0U) && (logger_stopping != 0)) {
            done = 1;
        } else {
            message = audit_queue[queue_head];
            queue_head = (queue_head + 1U) % AUDIT_QUEUE_CAPACITY;
            queue_count--;
            (void)pthread_cond_signal(&audit_space_condition);
        }
        (void)pthread_mutex_unlock(&audit_mutex);

        if (done == 0) {
            if (write_event(audit_file, message.actor, message.action) == 0) {
                (void)pthread_mutex_lock(&audit_mutex);
                if (logger_failed == 0) {
                    logger_failed = 1;
                    fprintf(stderr, "WARNING: could not write %s\n", AUDIT_FILE);
                }
                (void)pthread_cond_broadcast(&audit_space_condition);
                (void)pthread_mutex_unlock(&audit_mutex);
            }
        }
    }

    (void)pthread_mutex_lock(&audit_mutex);
    if (fclose(audit_file) != 0) {
        logger_failed = 1;
        fprintf(stderr, "WARNING: could not close %s\n", AUDIT_FILE);
    }
    audit_file = NULL;
    (void)pthread_mutex_unlock(&audit_mutex);
    return NULL;
}

int audit_logger_start(void)
{
    int ok = 0;

    (void)pthread_mutex_lock(&audit_mutex);
    if ((logger_active == 0) && (logger_stopping == 0)) {
        audit_file = fopen(AUDIT_FILE, "a");
        if (audit_file != NULL) {
            queue_head = 0U;
            queue_count = 0U;
            logger_failed = 0;
            logger_stopping = 0;
            if (pthread_create(&audit_thread, NULL, audit_worker, NULL) == 0) {
                logger_started = 1;
                logger_active = 1;
                ok = 1;
            } else {
                (void)fclose(audit_file);
                audit_file = NULL;
            }
        }
    } else if (logger_active != 0) {
        ok = 1;
    }
    (void)pthread_mutex_unlock(&audit_mutex);
    return ok;
}

void audit_logger_stop(void)
{
    int join_worker = 0;

    (void)pthread_mutex_lock(&audit_mutex);
    if (logger_started != 0) {
        logger_active = 0;
        logger_stopping = 1;
        join_worker = 1;
        (void)pthread_cond_signal(&audit_condition);
        (void)pthread_cond_broadcast(&audit_space_condition);
    }
    (void)pthread_mutex_unlock(&audit_mutex);

    if (join_worker != 0) {
        (void)pthread_join(audit_thread, NULL);
        (void)pthread_mutex_lock(&audit_mutex);
        logger_started = 0;
        logger_stopping = 0;
        (void)pthread_mutex_unlock(&audit_mutex);
    }
}

int audit_log_event(const char *actor, const char *action)
{
    int ok = 0;
    const char *event_actor = (actor != NULL) ? actor : "anonymous";
    const char *event_action = (action != NULL) ? action : "";

    (void)pthread_mutex_lock(&audit_mutex);
    if (logger_active != 0) {
        if ((strlen(event_actor) < AUDIT_ACTOR_SIZE) &&
            (strlen(event_action) < AUDIT_ACTION_SIZE)) {
            while ((queue_count == AUDIT_QUEUE_CAPACITY) && (logger_active != 0) &&
                   (logger_failed == 0)) {
                (void)pthread_cond_wait(&audit_space_condition, &audit_mutex);
            }
        }
        if ((logger_active != 0) && (logger_failed == 0) &&
            (strlen(event_actor) < AUDIT_ACTOR_SIZE) &&
            (strlen(event_action) < AUDIT_ACTION_SIZE)) {
            const size_t tail = (queue_head + queue_count) % AUDIT_QUEUE_CAPACITY;
            AuditMessage *message = &audit_queue[tail];

            (void)snprintf(message->actor, sizeof(message->actor), "%s", event_actor);
            (void)snprintf(message->action, sizeof(message->action), "%s", event_action);
            queue_count++;
            ok = 1;
            (void)pthread_cond_signal(&audit_condition);
        }
    } else if (logger_stopping == 0) {
        FILE *file = fopen(AUDIT_FILE, "a");

        if (file != NULL) {
            ok = write_event(file, actor, action);
            if (fclose(file) != 0) {
                ok = 0;
            }
        }
    }
    (void)pthread_mutex_unlock(&audit_mutex);
    return ok;
}
