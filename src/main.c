/* main.c - the menus. Each menu is a loop that asks for a number and runs one action. */
#include "types.h"
#include "utils.h"
#include "donors.h"
#include "blood.h"
#include "users.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>

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
    } else {
        printf("Could not add the donor: %s.\n", result_text(result));
    }
}

static void deactivate_donor_action(const Database *db)
{
    int id = 0;
    Result result;

    if (ask_number("ID of the donor to deactivate: ", 1, 1000000, &id) == 0) {
        return;
    }
    result = deactivate_donor(db, id);
    if (result == RESULT_OK) {
        printf("Donor %d is now deactivated.\n", id);
        save_or_warn(db);
    } else {
        printf("Could not deactivate: %s.\n", result_text(result));
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
    result = delete_donor(db, id);
    if (result == RESULT_OK) {
        printf("Donor %d was deleted.\n", id);
        save_or_warn(db);
    } else {
        printf("Could not delete: %s.\n", result_text(result));
    }
}

static void donor_menu(Database *db)
{
    int choice = -1;

    while ((choice != 0) && (input_closed() == 0)) {
        printf("\n=== DONORS ===\n");
        printf("1) Add a donor\n");
        printf("2) List all donors\n");
        printf("3) Search donors by blood group\n");
        printf("4) Deactivate a donor\n");
        printf("5) Delete a donor\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 5, &choice) == 0) {
            choice = 0;
        }
        switch (choice) {
        case 1: add_donor_action(db); break;
        case 2: list_donors(db, -1); break;
        case 3: {
            int group = 0;

            if (ask_group("Blood group to search for: ", &group) != 0) {
                list_donors(db, group);
            }
            break;
        }
        case 4: deactivate_donor_action(db); break;
        case 5: delete_donor_action(db); break;
        default: break;
        }
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
        return;
    }
    printf("Donor: %s, blood group %s.\n", donor->name, group_name(donor->group));
    problem = donor_problem(donor, today());
    if (problem != NULL) {
        printf("%s\n", problem);
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
    } else {
        printf("Could not record the donation: %s.\n", result_text(result));
    }
}

static void donation_menu(Database *db)
{
    int choice = -1;

    while ((choice != 0) && (input_closed() == 0)) {
        printf("\n=== DONATIONS ===\n");
        printf("1) Record a donation\n");
        printf("2) Donation history of one donor\n");
        printf("3) List all donations\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 3, &choice) == 0) {
            choice = 0;
        }
        switch (choice) {
        case 1: record_donation_action(db); break;
        case 2: {
            int donor_id = 0;

            if (ask_number("Donor ID: ", 1, 1000000, &donor_id) != 0) {
                list_batches(db, donor_id, today());
            }
            break;
        }
        case 3: list_batches(db, 0, today()); break;
        default: break;
        }
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
        return;
    }
    result = mark_batch_tested(db, id, today());
    if (result == RESULT_OK) {
        printf("Batch %d can now be used.\n", id);
        save_or_warn(db);
    } else {
        printf("Could not change the batch: %s.\n", result_text(result));
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
        return;
    }
    result = discard_batch(db, id);
    if (result == RESULT_OK) {
        printf("Batch %d was discarded.\n", id);
        save_or_warn(db);
    } else {
        printf("Could not discard: %s.\n", result_text(result));
    }
}

static void inventory_menu(const Database *db)
{
    int choice = -1;

    while ((choice != 0) && (input_closed() == 0)) {
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
        switch (choice) {
        case 1: show_stock(db, today()); break;
        case 2: {
            int group = 0;

            if (ask_group("Blood group: ", &group) != 0) {
                printf("%s ready to use: %d ml\n", group_name(group), stock_for_group(db, group, today()));
            }
            break;
        }
        case 3: list_batches(db, 0, today()); break;
        case 4: test_batch_action(db); break;
        case 5: discard_batch_action(db); break;
        default: break;
        }
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
    } else {
        printf("Could not save the request: %s.\n", result_text(result));
    }
}

static void process_request_action(const Database *db)
{
    int id = 0;
    Result result;

    if (ask_number("Request ID to fulfil: ", 1, 1000000, &id) == 0) {
        return;
    }
    result = process_request(db, id, today());
    if (result == RESULT_OK) {
        printf("Request %d fulfilled. The blood was taken from the stock.\n", id);
        save_or_warn(db);
    } else {
        printf("Request %d was NOT fulfilled: %s.\n", id, result_text(result));
    }
}

static void reject_request_action(const Database *db)
{
    int id = 0;
    Result result;

    if (ask_number("Request ID to reject: ", 1, 1000000, &id) == 0) {
        return;
    }
    result = reject_request(db, id);
    if (result == RESULT_OK) {
        printf("Request %d rejected.\n", id);
        save_or_warn(db);
    } else {
        printf("Could not reject: %s.\n", result_text(result));
    }
}

static void request_menu(Database *db)
{
    int choice = -1;

    while ((choice != 0) && (input_closed() == 0)) {
        printf("\n=== BLOOD REQUESTS ===\n");
        printf("1) New request\n");
        printf("2) Fulfil a request (give the blood)\n");
        printf("3) Reject a request\n");
        printf("4) List requests\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 4, &choice) == 0) {
            choice = 0;
        }
        switch (choice) {
        case 1: new_request_action(db); break;
        case 2: process_request_action(db); break;
        case 3: reject_request_action(db); break;
        case 4: list_requests(db); break;
        default: break;
        }
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
    if (read_password("New password: ", password, size) != 0) {
        if (read_password("Repeat the password: ", again, (int)sizeof(again)) != 0) {
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
        } else {
            printf("Could not create the user: %s (is the password strong enough?).\n",
                   result_text(result));
        }
    }
    wipe(password, (int)sizeof(password));
}

static void change_password_action(const Database *db, User *me)
{
    char old_password[PASSWORD_LEN];
    char new_password[PASSWORD_LEN];

    printf("\n--- Change my password ---\n");
    if (read_password("Current password: ", old_password, (int)sizeof(old_password)) != 0) {
        if (ask_new_password(new_password, (int)sizeof(new_password)) != 0) {
            Result result = change_password(me, old_password, new_password);
            if (result == RESULT_OK) {
                printf("Password changed.\n");
                save_or_warn(db);
            } else {
                printf("Password NOT changed: wrong current password, or the new one is weak or the same.\n");
            }
        }
    }
    wipe(old_password, (int)sizeof(old_password));
    wipe(new_password, (int)sizeof(new_password));
}

static void account_menu(Database *db, User *me)
{
    int choice = -1;

    while ((choice != 0) && (input_closed() == 0)) {
        printf("\n=== ACCOUNTS ===\n");
        printf("1) Add a user\n");
        printf("2) Change my password\n");
        printf("0) Back\n");
        if (ask_number("Choose: ", 0, 2, &choice) == 0) {
            choice = 0;
        }
        switch (choice) {
        case 1: add_user_action(db); break;
        case 2: change_password_action(db, me); break;
        default: break;
        }
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
        } else {
            printf("Could not create the user: %s.\n", result_text(result));
        }
    }
    wipe(password, (int)sizeof(password));
}

/* Gives the user three tries. Returns the logged-in user, or NULL. */
static User *login(const Database *db)
{
    User *user = NULL;
    int attempt = 0;

    while ((attempt < MAX_LOGIN_ATTEMPTS) && (user == NULL) && (input_closed() == 0)) {
        char username[USERNAME_LEN];

        if (read_line("Username: ", username, (int)sizeof(username)) != 0) {
            char password[PASSWORD_LEN];

            if (read_password("Password: ", password, (int)sizeof(password)) != 0) {
                User *found = find_user(db, username);

                if ((found != NULL) && (check_password(found, password) != 0)) {
                    user = found;
                    printf("Welcome, %s!\n", username);
                } else {
                    printf("Wrong username or password.\n");
                }
            }
            wipe(password, (int)sizeof(password));
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

    while ((choice != 0) && (input_closed() == 0)) {
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
}

int main(void)
{
    Database db = { NULL, NULL, NULL, NULL };
    User *me = NULL;
    int exit_code = 1;

    if (load_all(&db) == 0) {
        fprintf(stderr, "The data files are damaged. Nothing was changed. Restore them and try again.\n");
    } else {
        if (update_donor_statuses(&db, today()) > 0) {
            save_or_warn(&db);
        }
        if (db.users == NULL) {
            first_run_setup(&db);
        }
        if (db.users != NULL) {
            printf("\nBlood Bank - please log in.\n");
            me = login(&db);
        }
        if (me != NULL) {
            main_menu(&db, me);
            exit_code = 0;
        }
    }
    free_database(&db);
    return exit_code;
}
