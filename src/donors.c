/* donors.c - the linked list of donors. */
#include "types.h"
#include "utils.h"
#include "donors.h"

#include <stdio.h>
#include <stdlib.h>

/* Walks along the list until it finds the donor with this ID. */
Donor *find_donor(const Database *db, int id)
{
    Donor *current = db->donors;

    while ((current != NULL) && (current->id != id)) {
        current = current->next;
    }
    return current;
}

static int next_donor_id(const Database *db)
{
    int highest = 0;
    Donor *current = db->donors;
    const Batch *batch = db->batches;

    while (current != NULL) {
        if (current->id > highest) {
            highest = current->id;
        }
        current = current->next;
    }
    while (batch != NULL) {
        if (batch->donor_id > highest) {
            highest = batch->donor_id;
        }
        batch = batch->next;
    }
    return highest + 1;
}

/* Makes a copy of a donor and adds it to the END of the list. */
Donor *append_donor(Database *db, const Donor *source)
{
    Donor *node = malloc(sizeof(Donor));

    if (node != NULL) {
        *node = *source;
        node->next = NULL;
        if (db->donors == NULL) {
            db->donors = node;
        } else {
            Donor *last = db->donors;

            while (last->next != NULL) {
                last = last->next;
            }
            last->next = node;
        }
    }
    return node;
}

Result add_donor(Database *db, const char *name, const char *phone, int group,
                 int birth_year, int *new_id)
{
    Result result = RESULT_OK;
    Donor donor;

    donor.id = next_donor_id(db);
    donor.group = group;
    donor.birth_year = birth_year;
    donor.active = 1;
    donor.last_donation = 0L;
    donor.registered_at = today();
    donor.deactivated_at = 0L;
    if ((group < 0) || (group >= GROUP_COUNT) || (birth_year > current_year())) {
        result = RESULT_BAD_INPUT;
    } else if ((copy_text(donor.name, NAME_LEN, name) == 0) ||
               (copy_text(donor.phone, PHONE_LEN, phone) == 0)) {
        result = RESULT_BAD_INPUT;
    } else if (append_donor(db, &donor) == NULL) {
        result = RESULT_NO_MEMORY;
    } else {
        *new_id = donor.id;
    }
    return result;
}

Result deactivate_donor(Database *db, int id)
{
    Result result = RESULT_OK;
    Donor *donor = find_donor(db, id);

    if (donor == NULL) {
        result = RESULT_NOT_FOUND;
    } else if (donor->active == 0) {
        result = RESULT_WRONG_STATE;
    } else {
        donor->active = 0;
        donor->deactivated_at = today();
    }
    return result;
}

Result delete_donor(Database *db, int id)
{
    Donor **link = &db->donors;
    Result result = RESULT_NOT_FOUND;

    while ((*link != NULL) && ((*link)->id != id)) {
        link = &(*link)->next;
    }
    if (*link != NULL) {
        Donor *removed = *link;

        *link = removed->next;
        free(removed);
        result = RESULT_OK;
    }
    return result;
}

int update_donor_statuses(Database *db, long now)
{
    Donor **link = &db->donors;
    int changed = 0;

    while (*link != NULL) {
        Donor *donor = *link;

        if (donor->active == 0) {
            if ((donor->deactivated_at != 0L) &&
                ((now - donor->deactivated_at) >=
                 ((long)DONOR_DELETION_DAYS * SECONDS_PER_DAY))) {
                *link = donor->next;
                free(donor);
                changed++;
            } else {
                link = &donor->next;
            }
        } else {
            long last_activity = (donor->last_donation != 0L) ?
                                 donor->last_donation : donor->registered_at;

            if ((last_activity != 0L) &&
                ((now - last_activity) >=
                 ((long)DONOR_INACTIVE_DAYS * SECONDS_PER_DAY))) {
                donor->active = 0;
                donor->deactivated_at = now;
                changed++;
            }
            link = &donor->next;
        }
    }
    return changed;
}

/* Returns NULL if the donor may donate now, otherwise the reason why not. */
const char *donor_problem(const Donor *donor, long now)
{
    const char *problem = NULL;
    int age = current_year() - donor->birth_year;
    long wait_seconds = (long)DAYS_BETWEEN_DONATIONS * SECONDS_PER_DAY;

    if (donor->active == 0) {
        problem = "This donor is deactivated.";
    } else if ((age < MIN_AGE) || (age > MAX_AGE)) {
        problem = "Donors must be between 18 and 65 years old.";
    } else if ((donor->last_donation != 0L) && ((now - donor->last_donation) < wait_seconds)) {
        problem = "This donor gave blood less than 56 days ago.";
    } else {
        problem = NULL;
    }
    return problem;
}

/* Prints donors. Use group = -1 to print everybody. */
void list_donors(const Database *db, int group)
{
    const Donor *current = db->donors;
    int shown = 0;

    printf("\n%-4s %-24s %-17s %-6s %-6s %s\n", "ID", "Name", "Phone", "Group", "Born", "Status");
    while (current != NULL) {
        if ((group < 0) || (current->group == group)) {
            printf("%-4d %-24s %-17s %-6s %-6d %s\n", current->id, current->name, current->phone,
                   group_name(current->group), current->birth_year,
                   (current->active != 0) ? "active" : "deactivated");
            shown++;
        }
        current = current->next;
    }
    printf("%d donor(s) shown.\n", shown);
}
