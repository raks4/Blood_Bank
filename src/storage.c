/* storage.c - saving to and loading from plain text files.
   Each record is one line, fields separated by the '|' sign. */
#include "types.h"
#include "utils.h"
#include "donors.h"
#include "blood.h"
#include "users.h"
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DONORS_FILE    "donors.txt"
#define BATCHES_FILE   "batches.txt"
#define REQUESTS_FILE  "requests.txt"
#define USERS_FILE     "users.txt"

/* ---------------------------------------------------------------- */
/* Saving                                                            */
/* ---------------------------------------------------------------- */

/* We write to a temporary file first and rename it at the end, so a crash in the
   middle of saving can never leave a half-written data file behind. */
static int finish_save(FILE *file, const char *temporary_name, const char *final_name)
{
    int ok = (ferror(file) == 0) ? 1 : 0;

    if (fclose(file) != 0) {
        ok = 0;
    }
    if ((ok == 1) && (rename(temporary_name, final_name) != 0)) {
        ok = 0;
    }
    if (ok == 0) {
        fprintf(stderr, "ERROR: could not write %s\n", final_name);
    }
    return ok;
}

static int save_donors(const Database *db)
{
    int ok = 0;
    FILE *file = fopen("donors.tmp", "w");

    if (file == NULL) {
        fprintf(stderr, "ERROR: could not create donors.tmp\n");
    } else {
        const Donor *d = db->donors;

        while (d != NULL) {
            fprintf(file, "%d|%s|%s|%d|%d|%d|%ld|%ld|%ld\n", d->id, d->name, d->phone,
                    d->group, d->birth_year, d->active, d->last_donation, d->registered_at,
                    d->deactivated_at);
            d = d->next;
        }
        ok = finish_save(file, "donors.tmp", DONORS_FILE);
    }
    return ok;
}

static int save_batches(const Database *db)
{
    int ok = 0;
    FILE *file = fopen("batches.tmp", "w");

    if (file == NULL) {
        fprintf(stderr, "ERROR: could not create batches.tmp\n");
    } else {
        const Batch *b = db->batches;

        while (b != NULL) {
            fprintf(file, "%d|%d|%d|%d|%d|%ld|%ld|%d\n", b->id, b->donor_id, b->group,
                    b->ml_collected, b->ml_left, b->collected, b->expiry, b->status);
            b = b->next;
        }
        ok = finish_save(file, "batches.tmp", BATCHES_FILE);
    }
    return ok;
}

static int save_requests(const Database *db)
{
    int ok = 0;
    FILE *file = fopen("requests.tmp", "w");

    if (file == NULL) {
        fprintf(stderr, "ERROR: could not create requests.tmp\n");
    } else {
        const Request *r = db->requests;

        while (r != NULL) {
            fprintf(file, "%d|%s|%d|%d|%ld|%d\n", r->id, r->hospital, r->group, r->ml, r->date,
                    r->status);
            r = r->next;
        }
        ok = finish_save(file, "requests.tmp", REQUESTS_FILE);
    }
    return ok;
}

static int save_users(const Database *db)
{
    int ok = 0;
    FILE *file = fopen("users.tmp", "w");

    if (file == NULL) {
        fprintf(stderr, "ERROR: could not create users.tmp\n");
    } else {
        const User *u = db->users;

        (void)chmod("users.tmp", S_IRUSR | S_IWUSR);        /* only the owner may read it */
        while (u != NULL) {
            int i;

            fprintf(file, "%s|", u->username);
            for (i = 0; i < SALT_SIZE; i++) {
                fprintf(file, "%02x", (unsigned int)u->salt[i]);
            }
            fprintf(file, "|");
            for (i = 0; i < HASH_SIZE; i++) {
                fprintf(file, "%02x", (unsigned int)u->hash[i]);
            }
            fprintf(file, "\n");
            u = u->next;
        }
        ok = finish_save(file, "users.tmp", USERS_FILE);
    }
    return ok;
}

/* Saves everything. Returns 1 if all four files were written. */
int save_all(const Database *db)
{
    int donors_ok = save_donors(db);
    int batches_ok = save_batches(db);
    int requests_ok = save_requests(db);
    int users_ok = save_users(db);

    return ((donors_ok == 1) && (batches_ok == 1) && (requests_ok == 1) && (users_ok == 1)) ? 1 : 0;
}

/* ---------------------------------------------------------------- */
/* Loading. If a file is damaged we stop instead of guessing.        */
/* ---------------------------------------------------------------- */

/* Old donor rows have seven fields; new rows also store registration/deactivation times. */
static int read_donor(FILE *file, Donor *donor)
{
    char line[256];
    int fields;
    int consumed = 0;
    size_t length;

    do {
        size_t whitespace;

        if (fgets(line, (int)sizeof(line), file) == NULL) {
            return -1;
        }
        length = strlen(line);
        if ((length > 0U) && (line[length - 1U] == '\n')) {
            line[--length] = '\0';
        }
        if ((length > 0U) && (line[length - 1U] == '\r')) {
            line[--length] = '\0';
        }
        whitespace = strspn(line, " \t");
        if (whitespace == length) {
            continue;
        }
        break;
    } while (1);

    fields = sscanf(line, "%d|%49[^|]|%15[^|]|%d|%d|%d|%ld|%ld|%ld%n", &donor->id,
                    donor->name, donor->phone, &donor->group, &donor->birth_year, &donor->active,
                    &donor->last_donation, &donor->registered_at, &donor->deactivated_at, &consumed);
    if ((fields == 9) && (line[consumed] == '\0')) {
        return fields;
    }

    consumed = 0;
    fields = sscanf(line, "%d|%49[^|]|%15[^|]|%d|%d|%d|%ld%n", &donor->id, donor->name,
                    donor->phone, &donor->group, &donor->birth_year, &donor->active,
                    &donor->last_donation, &consumed);
    if ((fields == 7) && (line[consumed] == '\0')) {
        donor->registered_at = today();
        donor->deactivated_at = (donor->active != 0) ? 0L : today();
        return fields;
    }
    return 0;
}

static int read_batch(FILE *file, Batch *b)
{
    return fscanf(file, "%d|%d|%d|%d|%d|%ld|%ld|%d\n", &b->id, &b->donor_id, &b->group,
                  &b->ml_collected, &b->ml_left, &b->collected, &b->expiry, &b->status);
}

static int read_request(FILE *file, Request *r)
{
    return fscanf(file, "%d|%49[^|]|%d|%d|%ld|%d\n", &r->id, r->hospital, &r->group, &r->ml,
                  &r->date, &r->status);
}

static int group_is_valid(int group)
{
    return ((group >= 0) && (group < GROUP_COUNT)) ? 1 : 0;
}

static int load_donors(Database *db)
{
    int ok = 1;
    FILE *file = fopen(DONORS_FILE, "r");

    if (file == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "ERROR: cannot read %s\n", DONORS_FILE);
            ok = 0;
        }
    } else {
        Donor donor;
        int fields = read_donor(file, &donor);

        while (((fields == 7) || (fields == 9)) && (ok == 1)) {
            if ((donor.id < 1) || (find_donor(db, donor.id) != NULL) ||
                (group_is_valid(donor.group) == 0) || ((donor.active != 0) && (donor.active != 1)) ||
                (donor.registered_at < 0L) || (donor.deactivated_at < 0L) ||
                ((donor.active != 0) && (donor.deactivated_at != 0L)) ||
                ((donor.active == 0) && (donor.deactivated_at == 0L))) {
                ok = 0;
            } else if (append_donor(db, &donor) == NULL) {
                ok = 0;
            } else {
                fields = read_donor(file, &donor);
            }
        }
        if ((ok == 1) && (fields != -1)) {
            ok = 0;
        }
        (void)fclose(file);
    }
    return ok;
}

static int load_batches(Database *db)
{
    int ok = 1;
    FILE *file = fopen(BATCHES_FILE, "r");

    if (file == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "ERROR: cannot read %s\n", BATCHES_FILE);
            ok = 0;
        }
    } else {
        Batch batch;
        int fields = read_batch(file, &batch);

        while ((fields == 8) && (ok == 1)) {
            if ((batch.id < 1) || (find_batch(db, batch.id) != NULL) ||
                (group_is_valid(batch.group) == 0) || (batch.ml_left < 0) ||
                (batch.ml_left > batch.ml_collected) || (batch.status < BATCH_WAITING) ||
                (batch.status > BATCH_DISCARDED)) {
                ok = 0;
            } else if (append_batch(db, &batch) == NULL) {
                ok = 0;
            } else {
                fields = read_batch(file, &batch);
            }
        }
        if ((ok == 1) && (feof(file) == 0)) {
            ok = 0;
        }
        (void)fclose(file);
    }
    return ok;
}

static int load_requests(Database *db)
{
    int ok = 1;
    FILE *file = fopen(REQUESTS_FILE, "r");

    if (file == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "ERROR: cannot read %s\n", REQUESTS_FILE);
            ok = 0;
        }
    } else {
        Request request;
        int fields = read_request(file, &request);

        while ((fields == 6) && (ok == 1)) {
            if ((request.id < 1) || (find_request(db, request.id) != NULL) ||
                (group_is_valid(request.group) == 0) || (request.status < REQUEST_PENDING) ||
                (request.status > REQUEST_REJECTED)) {
                ok = 0;
            } else if (append_request(db, &request) == NULL) {
                ok = 0;
            } else {
                fields = read_request(file, &request);
            }
        }
        if ((ok == 1) && (feof(file) == 0)) {
            ok = 0;
        }
        (void)fclose(file);
    }
    return ok;
}

static int load_users(Database *db)
{
    int ok = 1;
    FILE *file = fopen(USERS_FILE, "r");

    if (file == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "ERROR: cannot read %s\n", USERS_FILE);
            ok = 0;
        }
    } else {
        User user;
        char salt_hex[SALT_SIZE * 2 + 1];
        char hash_hex[HASH_SIZE * 2 + 1];
        int fields = fscanf(file, "%19[^|]|%32[0-9a-f]|%64[0-9a-f]\n", user.username, salt_hex, hash_hex);

        while ((fields == 3) && (ok == 1)) {
            if ((hex_to_bytes(salt_hex, user.salt, SALT_SIZE) == 0) ||
                (hex_to_bytes(hash_hex, user.hash, HASH_SIZE) == 0) ||
                (find_user(db, user.username) != NULL) || (append_user(db, &user) == NULL)) {
                ok = 0;
            } else {
                fields = fscanf(file, "%19[^|]|%32[0-9a-f]|%64[0-9a-f]\n", user.username, salt_hex,
                                hash_hex);
            }
        }
        if ((ok == 1) && (feof(file) == 0)) {
            ok = 0;
        }
        (void)fclose(file);
    }
    return ok;
}

/* Loads everything. Returns 0 if any file is damaged. */
int load_all(Database *db)
{
    int ok = load_donors(db);

    if (ok == 1) {
        ok = load_batches(db);
    }
    if (ok == 1) {
        ok = load_requests(db);
    }
    if (ok == 1) {
        ok = load_users(db);
    }
    return ok;
}
