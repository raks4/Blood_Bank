/* main.c - the menus. Each menu is a loop that asks for a number and runs one action. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "types.h"
#include "utils.h"
#include "donors.h"
#include "blood.h"
#include "users.h"
#include "storage.h"
#include "audit.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAINTENANCE_INTERVAL_SECONDS 3600L

static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t maintenance_condition = PTHREAD_COND_INITIALIZER;
static pthread_t maintenance_thread;
static int maintenance_running;
static int maintenance_thread_started;

static void save_or_warn(const Database *db);

static const char *const group_names[GROUP_COUNT] = {
    "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-"
};

static const char *active_username = "anonymous";

static const char *group_name(int group)
{
    return ((group >= 0) && (group < GROUP_COUNT)) ? group_names[group] : "?";
}

static void log_action_for(const char *actor, const char *action)
{
    if (audit_log_event(actor, action) == 0) {
        fprintf(stderr, "WARNING: could not write %s\n", "audit.log");
    }
}

static void log_action(const char *action)
{
    log_action_for(active_username, action);
}

static void log_action_id(const char *action, int id)
{
    char details[80];

    (void)snprintf(details, sizeof(details), "%s (ID %d)", action, id);
    log_action(details);
}

static void *maintenance_worker(void *argument)
{
    Database *db = argument;
    int wait_error = 0;

    (void)pthread_mutex_lock(&db_mutex);
    while ((maintenance_running != 0) && (wait_error == 0)) {
        struct timespec deadline;

        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
            fprintf(stderr, "WARNING: could not schedule expiry monitoring\n");
            wait_error = 1;
        } else {
            int wait_result = 0;

            deadline.tv_sec += MAINTENANCE_INTERVAL_SECONDS;
            while ((maintenance_running != 0) && (wait_result == 0)) {
                wait_result = pthread_cond_timedwait(&maintenance_condition, &db_mutex, &deadline);
            }
            if ((wait_result != 0) && (wait_result != ETIMEDOUT)) {
                fprintf(stderr, "WARNING: expiry monitoring wait failed\n");
                wait_error = 1;
            } else if ((maintenance_running != 0) && (wait_result == ETIMEDOUT)) {
                const long now = today();
                const int donor_changes = update_donor_statuses(db, now);
                const int expired_batches = expire_batches(db, now);

                if ((donor_changes > 0) || (expired_batches > 0)) {
                    save_or_warn(db);
                }
                if (donor_changes > 0) {
                    log_action_for("system", "automatic donor status updates applied");
                }
                if (expired_batches > 0) {
                    log_action_for("system", "expired blood batches removed from stock");
                }
            }
        }
    }
    (void)pthread_mutex_unlock(&db_mutex);
    return NULL;
}

static void start_maintenance(Database *db)
{
    (void)pthread_mutex_lock(&db_mutex);
    if (maintenance_thread_started == 0) {
        maintenance_running = 1;
        if (pthread_create(&maintenance_thread, NULL, maintenance_worker, db) == 0) {
            maintenance_thread_started = 1;
        } else {
            maintenance_running = 0;
            fprintf(stderr, "WARNING: could not start expiry monitoring\n");
        }
    }
    (void)pthread_mutex_unlock(&db_mutex);
}

static void stop_maintenance(void)
{
    int join_thread = 0;

    (void)pthread_mutex_lock(&db_mutex);
    if (maintenance_thread_started != 0) {
        maintenance_running = 0;
        join_thread = 1;
        (void)pthread_cond_signal(&maintenance_condition);
    }
    (void)pthread_mutex_unlock(&db_mutex);

    if (join_thread != 0) {
        (void)pthread_join(maintenance_thread, NULL);
        (void)pthread_mutex_lock(&db_mutex);
        maintenance_thread_started = 0;
        (void)pthread_mutex_unlock(&db_mutex);
    }
}

static const char *result_text(Result result)
{
    switch (result) {
    case RESULT_OK: return "done";
    case RESULT_NOT_FOUND: return "no record with that ID";
    case RESULT_EXISTS: return "that already exists";
    case RESULT_BAD_INPUT: return "that value is not allowed";
    case RESULT_WRONG_STATE: return "not possible in the current state";
    case RESULT_NOT_ENOUGH_STOCK: return "not enough blood in stock";
    case RESULT_NO_MEMORY: return "out of memory";
    case RESULT_FAILED: return "internal error";
    default: return "unknown problem";
    }
}

static void wipe(char *buffer, int size)
{
    volatile char *p = buffer;

    for (int i = 0; i < size; i++) {
        p[i] = '\0';
    }
}

static int ask_text(const char *prompt, char *buffer, int size)
{
    return ask_input(prompt, INPUT_TEXT, buffer, size, NULL, 0, 0);
}

static int ask_phone(const char *prompt, char *buffer, int size)
{
    return ask_input(prompt, INPUT_PHONE, buffer, size, NULL, 0, 0);
}

static int ask_number(const char *prompt, int min, int max, int *number)
{
    return ask_input(prompt, INPUT_NUMBER, NULL, 0, number, min, max);
}

static int ask_group(const char *prompt, int *group)
{
    return ask_input(prompt, INPUT_GROUP, NULL, 0, group, 0, 0);
}

static int ask_yes(const char *prompt, int *yes)
{
    return ask_input(prompt, INPUT_YES, NULL, 0, yes, 0, 0);
}

/* Saves after every change, so nothing is lost if the program stops suddenly. */
static void save_or_warn(const Database *db)
{
    if (save_all(db) == 0) {
        fprintf(stderr, "WARNING: your changes could NOT be saved!\n");
    }
}

/* ================================================================ */
/* Donors                                                            */
/* ================================================================ */

static void add_donor_action(Database *db)
{
    char name[NAME_LEN];
    char phone[PHONE_LEN];
    int group = 0;
    int birth_year = 0;
    int new_id = 0;
    Result result;

    printf("\n--- Add a donor ---\n");
    if (ask_text("Full name: ", name, (int)sizeof(name)) == 0) {
        return;
    }
    if (ask_phone("Phone number: ", phone, (int)sizeof(phone)) == 0) {
        return;
    }
    if (ask_group("Blood group (A+ A- B+ B- AB+ AB- O+ O-): ", &group) == 0) {
        return;
    }
    if (ask_number("Year of birth: ", current_year() - 120, current_year(), &birth_year) == 0) {
        return;
    }
    result = add_donor(db, name, phone, group, birth_year, &new_id);
    if (result == RESULT_OK) {
        printf("Donor added. The donor's ID is %d.\n", new_id);
        save_or_warn(db);
        log_action_id("donor added", new_id);
    } else {
        printf("Could not add the donor: %s.\n", result_text(result));
        log_action("donor add failed");
    }
}

static void deactivate_donor_action(Database *db)
{
    int id = 0;
    Result result;

    if (ask_number("ID of the donor to deactivate: ", 1, 1000000, &id) == 0) {
        return;
    }
    result = update_donor(db, id, DONOR_DEACTIVATE);
    if (result == RESULT_OK) {
        printf("Donor %d is now deactivated.\n", id);
        save_or_warn(db);
        log_action_id("donor deactivated", id);
    } else {
        printf("Could not deactivate: %s.\n", result_text(result));
        log_action_id("donor deactivation failed", id);
    }
}

static void update_donor_details_action(const Database *db)
{
    int id = 0;
    char name[NAME_LEN];
    char phone[PHONE_LEN];
    int group = 0;
    int birth_year = 0;
    Result result;

    if (ask_number("ID of the donor to update: ", 1, 1000000, &id) == 0) {
        return;
    }
    if (find_donor(db, id) == NULL) {
        printf("Could not update the donor: %s.\n", result_text(RESULT_NOT_FOUND));
        log_action_id("donor update failed", id);
        return;
    }
    if (ask_text("Full name: ", name, (int)sizeof(name)) == 0) {
        return;
    }
    if (ask_phone("Phone number: ", phone, (int)sizeof(phone)) == 0) {
        return;
    }
    if (ask_group("Blood group (A+ A- B+ B- AB+ AB- O+ O-): ", &group) == 0) {
        return;
    }
    if (ask_number("Year of birth: ", current_year() - 120, current_year(), &birth_year) == 0) {
        return;
    }
    result = update_donor_details(db, id, name, phone, group, birth_year);
    if (result == RESULT_OK) {
        printf("Donor %d details updated.\n", id);
        save_or_warn(db);
        log_action_id("donor details updated", id);
    } else {
        printf("Could not update the donor: %s.\n", result_text(result));
        log_action_id("donor update failed", id);
    }
}

static void delete_donor_action(Database *db)
{
    int id = 0;
    int yes = 0;
    Result result;

    if (ask_number("ID of the donor to delete: ", 1, 1000000, &id) == 0) {
        return;
    }
    if (ask_yes("Delete this donor profile? Donation history will remain. (y/n): ", &yes) == 0) {
        return;
    }
    if (yes == 0) {
        printf("Cancelled.\n");
        return;
    }
    result = update_donor(db, id, DONOR_DELETE);
    if (result == RESULT_OK) {
        printf("Donor %d was deleted.\n", id);
        save_or_warn(db);
        log_action_id("donor deleted", id);
    } else {
        printf("Could not delete: %s.\n", result_text(result));
        log_action_id("donor deletion failed", id);
    }
}

static void donor_menu(Database *db)
{
    int choice = -1;

    while (choice != 0) {
        printf("\n=== DONORS ===\n");
        printf("1) Add a donor\n");
        printf("2) List all donors\n");
        printf("3) Search donors by blood group\n");
        printf("4) Update donor details\n");
        printf("5) Deactivate a donor\n");
        printf("6) Delete a donor\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 6, &choice) == 0) {
            choice = 0;
        }
        (void)pthread_mutex_lock(&db_mutex);
        switch (choice) {
        case 1: add_donor_action(db); break;
        case 2:
            list_donors(db, -1);
            log_action("donor list viewed");
            break;
        case 3: {
            int group = 0;

            if (ask_group("Blood group to search for: ", &group) != 0) {
                list_donors(db, group);
                log_action("donors searched by blood group");
            }
            break;
        }
        case 4: update_donor_details_action(db); break;
        case 5: deactivate_donor_action(db); break;
        case 6: delete_donor_action(db); break;
        default: break;
        }
        (void)pthread_mutex_unlock(&db_mutex);
    }
}

/* ================================================================ */
/* Donations                                                         */
/* ================================================================ */

static void record_donation_action(Database *db)
{
    int donor_id = 0;
    int ml = 0;
    int batch_id = 0;
    Donor *donor;
    const char *problem;
    Result result;

    printf("\n--- Record a donation ---\n");
    if (ask_number("Donor ID: ", 1, 1000000, &donor_id) == 0) {
        return;
    }
    donor = find_donor(db, donor_id);
    if (donor == NULL) {
        printf("There is no donor with ID %d.\n", donor_id);
        log_action_id("donation rejected for unknown donor", donor_id);
        return;
    }
    printf("Donor: %s, blood group %s.\n", donor->name, group_name(donor->group));
    problem = donor_problem(donor, today());
    if (problem != NULL) {
        printf("%s\n", problem);
        log_action_id("donation rejected for donor eligibility", donor_id);
        return;
    }
    if (ask_number("Amount of blood in ml: ", MIN_DONATION_ML, MAX_DONATION_ML, &ml) == 0) {
        return;
    }
    result = record_donation(db, donor, ml, today(), &batch_id);
    if (result == RESULT_OK) {
        printf("Donation saved as batch %d. It can be used after the blood tests are marked as done.\n",
               batch_id);
        save_or_warn(db);
        log_action_id("donation recorded as batch", batch_id);
    } else {
        printf("Could not record the donation: %s.\n", result_text(result));
        log_action_id("donation recording failed for donor", donor_id);
    }
}

static void donation_menu(Database *db)
{
    int choice = -1;

    while (choice != 0) {
        printf("\n=== DONATIONS ===\n");
        printf("1) Record a donation\n");
        printf("2) Donation history of one donor\n");
        printf("3) List all donations\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 3, &choice) == 0) {
            choice = 0;
        }
        (void)pthread_mutex_lock(&db_mutex);
        switch (choice) {
        case 1: record_donation_action(db); break;
        case 2: {
            int donor_id = 0;

            if (ask_number("Donor ID: ", 1, 1000000, &donor_id) != 0) {
                list_batches(db, donor_id, today());
                log_action_id("donation history viewed for donor", donor_id);
            }
            break;
        }
        case 3:
            list_batches(db, 0, today());
            log_action("donation list viewed");
            break;
        default: break;
        }
        (void)pthread_mutex_unlock(&db_mutex);
    }
}

/* ================================================================ */
/* Inventory                                                         */
/* ================================================================ */

static void test_batch_action(const Database *db)
{
    int id = 0;
    int yes = 0;
    Result result;

    if (ask_number("Batch ID: ", 1, 1000000, &id) == 0) {
        return;
    }
    if (ask_yes("Did ALL blood tests pass? (y/n): ", &yes) == 0) {
        return;
    }
    if (yes == 0) {
        printf("Cancelled. Use 'Discard a batch' if the blood must not be used.\n");
        log_action_id("batch test cancelled", id);
        return;
    }
    result = update_batch(db, id, BATCH_TEST, today());
    if (result == RESULT_OK) {
        printf("Batch %d can now be used.\n", id);
        save_or_warn(db);
        log_action_id("batch marked tested", id);
    } else {
        printf("Could not change the batch: %s.\n", result_text(result));
        log_action_id("batch testing failed", id);
    }
}

static void discard_batch_action(const Database *db)
{
    int id = 0;
    int yes = 0;
    Result result;

    if (ask_number("Batch ID: ", 1, 1000000, &id) == 0) {
        return;
    }
    if (ask_yes("Discard this batch for good? (y/n): ", &yes) == 0) {
        return;
    }
    if (yes == 0) {
        printf("Cancelled.\n");
        log_action_id("batch discard cancelled", id);
        return;
    }
    result = update_batch(db, id, BATCH_DISCARD, today());
    if (result == RESULT_OK) {
        printf("Batch %d was discarded.\n", id);
        save_or_warn(db);
        log_action_id("batch discarded", id);
    } else {
        printf("Could not discard: %s.\n", result_text(result));
        log_action_id("batch discard failed", id);
    }
}

static void inventory_menu(const Database *db)
{
    int choice = -1;

    while (choice != 0) {
        printf("\n=== INVENTORY ===\n");
        printf("1) Stock of every blood group\n");
        printf("2) Check one blood group\n");
        printf("3) List all batches\n");
        printf("4) Mark a batch as tested (OK to use)\n");
        printf("5) Discard a batch\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 5, &choice) == 0) {
            choice = 0;
        }
        (void)pthread_mutex_lock(&db_mutex);
        switch (choice) {
        case 1:
            show_stock(db, today());
            log_action("full blood stock viewed");
            break;
        case 2: {
            int group = 0;

            if (ask_group("Blood group: ", &group) != 0) {
                printf("%s ready to use: %d ml\n", group_name(group), stock_for_group(db, group, today()));
                log_action("blood stock checked by group");
            }
            break;
        }
        case 3:
            list_batches(db, 0, today());
            log_action("batch list viewed");
            break;
        case 4: test_batch_action(db); break;
        case 5: discard_batch_action(db); break;
        default: break;
        }
        (void)pthread_mutex_unlock(&db_mutex);
    }
}

/* ================================================================ */
/* Requests                                                          */
/* ================================================================ */

static void new_request_action(Database *db)
{
    char hospital[NAME_LEN];
    int group = 0;
    int ml = 0;
    int new_id = 0;
    Result result;

    printf("\n--- New request from a hospital ---\n");
    if (ask_text("Hospital name: ", hospital, (int)sizeof(hospital)) == 0) {
        return;
    }
    if (ask_group("Blood group needed: ", &group) == 0) {
        return;
    }
    if (ask_number("Amount in ml: ", MIN_REQUEST_ML, MAX_REQUEST_ML, &ml) == 0) {
        return;
    }
    result = add_request(db, hospital, group, ml, today(), &new_id);
    if (result == RESULT_OK) {
        printf("Request %d saved. Ready to use now: %d ml of %s.\n", new_id,
               stock_for_group(db, group, today()), group_name(group));
        save_or_warn(db);
        log_action_id("blood request created", new_id);
    } else {
        printf("Could not save the request: %s.\n", result_text(result));
        log_action("blood request creation failed");
    }
}

static void process_request_action(const Database *db)
{
    int id = 0;
    Result result;

    if (ask_number("Request ID to fulfil: ", 1, 1000000, &id) == 0) {
        return;
    }
    result = update_request(db, id, REQUEST_FULFILL, today());
    if (result == RESULT_OK) {
        printf("Request %d fulfilled. The blood was taken from the stock.\n", id);
        save_or_warn(db);
        log_action_id("blood request fulfilled", id);
    } else {
        printf("Request %d was NOT fulfilled: %s.\n", id, result_text(result));
        log_action_id("blood request fulfillment failed", id);
    }
}

static void reject_request_action(const Database *db)
{
    int id = 0;
    Result result;

    if (ask_number("Request ID to reject: ", 1, 1000000, &id) == 0) {
        return;
    }
    result = update_request(db, id, REQUEST_REJECT, today());
    if (result == RESULT_OK) {
        printf("Request %d rejected.\n", id);
        save_or_warn(db);
        log_action_id("blood request rejected", id);
    } else {
        printf("Could not reject: %s.\n", result_text(result));
        log_action_id("blood request rejection failed", id);
    }
}

static void request_menu(Database *db)
{
    int choice = -1;

    while (choice != 0) {
        printf("\n=== BLOOD REQUESTS ===\n");
        printf("1) New request\n");
        printf("2) Fulfil a request (give the blood)\n");
        printf("3) Reject a request\n");
        printf("4) List requests\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 4, &choice) == 0) {
            choice = 0;
        }
        (void)pthread_mutex_lock(&db_mutex);
        switch (choice) {
        case 1: new_request_action(db); break;
        case 2: process_request_action(db); break;
        case 3: reject_request_action(db); break;
        case 4:
            list_requests(db);
            log_action("request list viewed");
            break;
        default: break;
        }
        (void)pthread_mutex_unlock(&db_mutex);
    }
}

/* ================================================================ */
/* Accounts and login                                                */
/* ================================================================ */

/* Asks for a new password twice. Returns 1 if both are the same. */
static int ask_new_password(char *password, int size)
{
    char again[PASSWORD_LEN];
    int ok = 0;

    printf("A password needs at least 8 characters, with a letter and a digit.\n");
    if (read_input("New password: ", password, size, 1) != 0) {
        if (read_input("Repeat the password: ", again, (int)sizeof(again), 1) != 0) {
            if (strcmp(password, again) == 0) {
                ok = 1;
            } else {
                printf("The two passwords are different.\n");
            }
        }
    }
    wipe(again, (int)sizeof(again));
    return ok;
}

static void add_user_action(Database *db)
{
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];

    printf("\n--- Add a user ---\n");
    if (ask_text("New username: ", username, (int)sizeof(username)) == 0) {
        return;
    }
    if (ask_new_password(password, (int)sizeof(password)) != 0) {
        Result result = create_user(db, username, password);
        if (result == RESULT_OK) {
            printf("User %s created.\n", username);
            save_or_warn(db);
            log_action("user account created");
        } else {
            printf("Could not create the user: %s (is the password strong enough?).\n",
                   result_text(result));
            log_action("user account creation failed");
        }
    }
    wipe(password, (int)sizeof(password));
}

static void change_password_action(const Database *db, User *me)
{
    char old_password[PASSWORD_LEN];
    char new_password[PASSWORD_LEN];

    printf("\n--- Change my password ---\n");
    if (read_input("Current password: ", old_password, (int)sizeof(old_password), 1) != 0) {
        if (ask_new_password(new_password, (int)sizeof(new_password)) != 0) {
            Result result = change_password(me, old_password, new_password);
            if (result == RESULT_OK) {
                printf("Password changed.\n");
                save_or_warn(db);
                log_action("password changed");
            } else {
                printf("Password NOT changed: wrong current password, or the new one is weak or the same.\n");
                log_action("password change failed");
            }
        }
    }
    wipe(old_password, (int)sizeof(old_password));
    wipe(new_password, (int)sizeof(new_password));
}

static void delete_user_action(Database *db, const User *me)
{
    char username[USERNAME_LEN];
    int yes = 0;
    Result result;

    if (ask_text("Username of the account to delete: ", username, (int)sizeof(username)) == 0) {
        return;
    }
    if (strcmp(username, me->username) == 0) {
        printf("You cannot delete the account you are currently using.\n");
        log_action("user account deletion rejected for current user");
        return;
    }
    if (ask_yes("Delete this user account? (y/n): ", &yes) == 0) {
        return;
    }
    if (yes == 0) {
        printf("Cancelled.\n");
        return;
    }
    result = delete_user(db, username);
    if (result == RESULT_OK) {
        printf("User %s deleted.\n", username);
        save_or_warn(db);
        log_action("user account deleted");
    } else {
        printf("Could not delete the user: %s.\n", result_text(result));
        log_action("user account deletion failed");
    }
}

static void account_menu(Database *db, User *me)
{
    int choice = -1;

    while (choice != 0) {
        printf("\n=== ACCOUNTS ===\n");
        printf("1) Add a user\n");
        printf("2) Change my password\n");
        printf("3) Delete a user account\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 3, &choice) == 0) {
            choice = 0;
        }
        (void)pthread_mutex_lock(&db_mutex);
        switch (choice) {
        case 1: add_user_action(db); break;
        case 2: change_password_action(db, me); break;
        case 3: delete_user_action(db, me); break;
        default: break;
        }
        (void)pthread_mutex_unlock(&db_mutex);
    }
}

/* The very first time there is no user yet, so we create one. */
static void first_run_setup(Database *db)
{
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];

    printf("Welcome! No users exist yet, so let us create the first one.\n");
    if (ask_text("Choose a username: ", username, (int)sizeof(username)) == 0) {
        return;
    }
    if (ask_new_password(password, (int)sizeof(password)) != 0) {
        Result result = create_user(db, username, password);
        if (result == RESULT_OK) {
            printf("User %s created.\n", username);
            save_or_warn(db);
            log_action_for(username, "first user account created");
        } else {
            printf("Could not create the user: %s.\n", result_text(result));
            log_action_for(username, "first user account creation failed");
        }
    }
    wipe(password, (int)sizeof(password));
}

/* Gives the user three tries. Returns the logged-in user, or NULL. */
static User *login(const Database *db)
{
    User *user = NULL;
    int attempt = 0;

    while ((attempt < MAX_LOGIN_ATTEMPTS) && (user == NULL)) {
        char username[USERNAME_LEN];

        if (read_input("Username: ", username, (int)sizeof(username), 0) != 0) {
            char password[PASSWORD_LEN];

            if (read_input("Password: ", password, (int)sizeof(password), 1) != 0) {
                user = authenticate_user(db, username, password);
                if (user != NULL) {
                    printf("Welcome, %s!\n", username);
                    log_action_for(username, "login successful");
                } else {
                    printf("Wrong username or password.\n");
                    log_action_for(username, "login failed");
                }
            } else {
                wipe(password, (int)sizeof(password));
                break;
            }
            wipe(password, (int)sizeof(password));
        } else {
            break;
        }
        attempt++;
    }
    return user;
}

/* ================================================================ */
/* Main menu                                                         */
/* ================================================================ */

static void main_menu(Database *db, User *me)
{
    int choice = -1;

    while (choice != 0) {
        printf("\n===== BLOOD BANK =====\n");
        printf("1) Donors\n");
        printf("2) Donations\n");
        printf("3) Inventory\n");
        printf("4) Blood requests\n");
        printf("5) Accounts\n");
        printf("0) Exit\n");
        if (ask_number("Choose: ", 0, 5, &choice) == 0) {
            choice = 0;
        }
        switch (choice) {
        case 1: donor_menu(db); break;
        case 2: donation_menu(db); break;
        case 3: inventory_menu(db); break;
        case 4: request_menu(db); break;
        case 5: account_menu(db, me); break;
        default: break;
        }
    }
    printf("Goodbye!\n");
    log_action("logout");
}

int main(void)
{
    Database db = { NULL, NULL, NULL, NULL };
    User *me = NULL;
    int exit_code = 1;

    if (load_all(&db) == 0) {
        fprintf(stderr, "The data files are damaged. Nothing was changed. Restore them and try again.\n");
    } else {
        if (audit_logger_start() == 0) {
            fprintf(stderr, "WARNING: could not start asynchronous audit logging\n");
        }
        {
            const long now = today();
            const int donor_changes = update_donor_statuses(&db, now);
            const int expired_batches = expire_batches(&db, now);

            if ((donor_changes > 0) || (expired_batches > 0)) {
                save_or_warn(&db);
            }
            if (donor_changes > 0) {
                log_action_for("system", "automatic donor status updates applied");
            }
            if (expired_batches > 0) {
                log_action_for("system", "expired blood batches removed from stock");
            }
        }
        if (db.users == NULL) {
            first_run_setup(&db);
        }
        if (db.users != NULL) {
            printf("\nBlood Bank - please log in.\n");
            me = login(&db);
        }
        if (me != NULL) {
            active_username = me->username;
            start_maintenance(&db);
            main_menu(&db, me);
            stop_maintenance();
            exit_code = 0;
        }
        audit_logger_stop();
    }
    free_database(&db);
    return exit_code;
}
