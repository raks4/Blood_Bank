/* blood.c - blood batches (donations), stock, and hospital requests. */
#include "types.h"
#include "utils.h"
#include "blood.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- */
/* Batches: one batch = one donation = one bag of blood              */
/* ---------------------------------------------------------------- */

Batch *find_batch(const Database *db, int id)
{
    Batch *current = db->batches;

    while ((current != NULL) && (current->id != id)) {
        current = current->next;
    }
    return current;
}

static int next_batch_id(const Database *db)
{
    int highest = 0;
    Batch *current = db->batches;

    while (current != NULL) {
        if (current->id > highest) {
            highest = current->id;
        }
        current = current->next;
    }
    return highest + 1;
}

Batch *append_batch(Database *db, const Batch *source)
{
    Batch *node = malloc(sizeof(Batch));

    if (node != NULL) {
        *node = *source;
        node->next = NULL;
        if (db->batches == NULL) {
            db->batches = node;
        } else {
            Batch *last = db->batches;

            while (last->next != NULL) {
                last = last->next;
            }
            last->next = node;
        }
    }
    return node;
}

/* A new donation waits for blood tests before it can be used. */
Result record_donation(Database *db, Donor *donor, int ml, long now, int *batch_id)
{
    Result result = RESULT_OK;
    Batch batch;

    if ((ml < MIN_DONATION_ML) || (ml > MAX_DONATION_ML)) {
        result = RESULT_BAD_INPUT;
    } else {
        batch.id = next_batch_id(db);
        batch.donor_id = donor->id;
        batch.group = donor->group;
        batch.ml_collected = ml;
        batch.ml_left = ml;
        batch.collected = now;
        batch.expiry = now + ((long)SHELF_LIFE_DAYS * SECONDS_PER_DAY);
        batch.status = BATCH_WAITING;
        if (append_batch(db, &batch) == NULL) {
            result = RESULT_NO_MEMORY;
        } else {
            donor->last_donation = now;
            *batch_id = batch.id;
        }
    }
    return result;
}

/* Called when the blood tests are fine: the batch becomes usable. */
Result mark_batch_tested(Database *db, int id, long now)
{
    Result result = RESULT_OK;
    Batch *batch = find_batch(db, id);

    if (batch == NULL) {
        result = RESULT_NOT_FOUND;
    } else if ((batch->status != BATCH_WAITING) || (batch->expiry < now)) {
        result = RESULT_WRONG_STATE;
    } else {
        batch->status = BATCH_AVAILABLE;
    }
    return result;
}

Result discard_batch(Database *db, int id)
{
    Result result = RESULT_OK;
    Batch *batch = find_batch(db, id);

    if (batch == NULL) {
        result = RESULT_NOT_FOUND;
    } else if (batch->status == BATCH_DISCARDED) {
        result = RESULT_WRONG_STATE;
    } else {
        batch->status = BATCH_DISCARDED;
        batch->ml_left = 0;
    }
    return result;
}

/* Can this batch be given to a hospital today? */
static int batch_is_usable(const Batch *batch, long now)
{
    return ((batch->status == BATCH_AVAILABLE) && (batch->ml_left > 0) &&
            (batch->expiry >= now)) ? 1 : 0;
}

/* How many ml of this blood group can be given out today? */
int stock_for_group(const Database *db, int group, long now)
{
    int total = 0;
    const Batch *current = db->batches;

    while (current != NULL) {
        if ((current->group == group) && (batch_is_usable(current, now) != 0)) {
            total = total + current->ml_left;
        }
        current = current->next;
    }
    return total;
}

/* The usable batch of this group that expires first (old blood is used first). */
static Batch *oldest_usable_batch(const Database *db, int group, long now)
{
    Batch *best = NULL;
    Batch *current = db->batches;

    while (current != NULL) {
        if ((current->group == group) && (batch_is_usable(current, now) != 0)) {
            if ((best == NULL) || (current->expiry < best->expiry)) {
                best = current;
            }
        }
        current = current->next;
    }
    return best;
}

static const char *batch_status_text(const Batch *batch, long now)
{
    const char *text = "available";

    if (batch->status == BATCH_DISCARDED) {
        text = "discarded";
    } else if (batch->expiry < now) {
        text = "expired";
    } else if (batch->status == BATCH_WAITING) {
        text = "waiting for tests";
    } else if (batch->ml_left == 0) {
        text = "empty";
    } else {
        text = "available";
    }
    return text;
}

/* Prints how much blood of each group is in stock. */
void show_stock(const Database *db, long now)
{
    int group;

    printf("\n%-6s %-18s %-20s %s\n", "Group", "Ready to use (ml)", "Waiting for tests (ml)", "");
    for (group = 0; group < GROUP_COUNT; group++) {
        int ready = stock_for_group(db, group, now);
        int waiting = 0;
        const Batch *current = db->batches;

        while (current != NULL) {
            if ((current->group == group) && (current->status == BATCH_WAITING) &&
                (current->expiry >= now)) {
                waiting = waiting + current->ml_left;
            }
            current = current->next;
        }
        printf("%-6s %-18d %-22d %s\n", group_name(group), ready, waiting,
               (ready < LOW_STOCK_ML) ? "LOW STOCK" : "");
    }
}

/* Prints batches. Use donor_id = 0 to print all of them. */
void list_batches(const Database *db, int donor_id, long now)
{
    const Batch *current = db->batches;
    int shown = 0;

    printf("\n%-5s %-6s %-6s %-9s %-8s %-11s %-11s %s\n", "Batch", "Donor", "Group", "Collected",
           "Left ml", "Donated", "Expires", "Status");
    while (current != NULL) {
        if ((donor_id == 0) || (current->donor_id == donor_id)) {
            char donated[16];
            char expires[16];

            format_date(current->collected, donated, (int)sizeof(donated));
            format_date(current->expiry, expires, (int)sizeof(expires));
            printf("%-5d %-6d %-6s %-9d %-8d %-11s %-11s %s\n", current->id, current->donor_id,
                   group_name(current->group), current->ml_collected, current->ml_left, donated,
                   expires, batch_status_text(current, now));
            shown++;
        }
        current = current->next;
    }
    printf("%d batch(es) shown.\n", shown);
}

/* ---------------------------------------------------------------- */
/* Requests from hospitals                                           */
/* ---------------------------------------------------------------- */

Request *find_request(const Database *db, int id)
{
    Request *current = db->requests;

    while ((current != NULL) && (current->id != id)) {
        current = current->next;
    }
    return current;
}

static int next_request_id(const Database *db)
{
    int highest = 0;
    Request *current = db->requests;

    while (current != NULL) {
        if (current->id > highest) {
            highest = current->id;
        }
        current = current->next;
    }
    return highest + 1;
}

Request *append_request(Database *db, const Request *source)
{
    Request *node = malloc(sizeof(Request));

    if (node != NULL) {
        *node = *source;
        node->next = NULL;
        if (db->requests == NULL) {
            db->requests = node;
        } else {
            Request *last = db->requests;

            while (last->next != NULL) {
                last = last->next;
            }
            last->next = node;
        }
    }
    return node;
}

Result add_request(Database *db, const char *hospital, int group, int ml, long now, int *new_id)
{
    Result result = RESULT_OK;
    Request request;

    request.id = next_request_id(db);
    request.group = group;
    request.ml = ml;
    request.date = now;
    request.status = REQUEST_PENDING;
    if ((group < 0) || (group >= GROUP_COUNT) || (ml < MIN_REQUEST_ML) || (ml > MAX_REQUEST_ML)) {
        result = RESULT_BAD_INPUT;
    } else if (copy_text(request.hospital, NAME_LEN, hospital) == 0) {
        result = RESULT_BAD_INPUT;
    } else if (append_request(db, &request) == NULL) {
        result = RESULT_NO_MEMORY;
    } else {
        *new_id = request.id;
    }
    return result;
}

/* Gives the blood to the hospital: takes it from the batches that expire first. */
Result process_request(Database *db, int id, long now)
{
    Result result = RESULT_OK;
    Request *request = find_request(db, id);

    if (request == NULL) {
        result = RESULT_NOT_FOUND;
    } else if (request->status != REQUEST_PENDING) {
        result = RESULT_WRONG_STATE;
    } else if (stock_for_group(db, request->group, now) < request->ml) {
        result = RESULT_NOT_ENOUGH_STOCK;
    } else {
        int still_needed = request->ml;

        while (still_needed > 0) {
            Batch *batch = oldest_usable_batch(db, request->group, now);
            int taken;

            if (batch == NULL) {
                still_needed = 0;               /* cannot happen: stock was checked above */
            } else {
                taken = (batch->ml_left < still_needed) ? batch->ml_left : still_needed;
                batch->ml_left = batch->ml_left - taken;
                still_needed = still_needed - taken;
            }
        }
        request->status = REQUEST_FULFILLED;
    }
    return result;
}

Result reject_request(Database *db, int id)
{
    Result result = RESULT_OK;
    Request *request = find_request(db, id);

    if (request == NULL) {
        result = RESULT_NOT_FOUND;
    } else if (request->status != REQUEST_PENDING) {
        result = RESULT_WRONG_STATE;
    } else {
        request->status = REQUEST_REJECTED;
    }
    return result;
}

void list_requests(const Database *db)
{
    const Request *current = db->requests;
    int shown = 0;

    printf("\n%-4s %-24s %-6s %-8s %-11s %s\n", "ID", "Hospital", "Group", "ml", "Date", "Status");
    while (current != NULL) {
        char date[16];
        const char *status = "pending";

        format_date(current->date, date, (int)sizeof(date));
        if (current->status == REQUEST_FULFILLED) {
            status = "fulfilled";
        } else if (current->status == REQUEST_REJECTED) {
            status = "rejected";
        } else {
            status = "pending";
        }
        printf("%-4d %-24s %-6s %-8d %-11s %s\n", current->id, current->hospital,
               group_name(current->group), current->ml, date, status);
        shown++;
        current = current->next;
    }
    printf("%d request(s) shown.\n", shown);
}

/* ---------------------------------------------------------------- */
/* Giving all memory back when the program ends                      */
/* ---------------------------------------------------------------- */

void free_database(Database *db)
{
    while (db->donors != NULL) {
        Donor *next = db->donors->next;

        free(db->donors);
        db->donors = next;
    }
    while (db->batches != NULL) {
        Batch *next = db->batches->next;

        free(db->batches);
        db->batches = next;
    }
    while (db->requests != NULL) {
        Request *next = db->requests->next;

        free(db->requests);
        db->requests = next;
    }
    while (db->users != NULL) {
        User *next = db->users->next;

        free(db->users);
        db->users = next;
    }
}
