/* users.c - login accounts. Passwords are never stored, only a salted hash. */
#include "types.h"
#include "utils.h"
#include "users.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdlib.h>
#include <string.h>

#define HASH_ROUNDS 600000      /* slow on purpose, so guessing passwords is slow */

static int copy_text(char *destination, int size, const char *source)
{
    int length = (int)strlen(source);

    if (length >= size) {
        return 0;
    }
    (void)memcpy(destination, source, (size_t)length + 1U);
    return 1;
}

static User *find_user(const Database *db, const char *username)
{
    User *current = db->users;

    while ((current != NULL) && (strcmp(current->username, username) != 0)) {
        current = current->next;
    }
    return current;
}

static User *append_user(Database *db, const User *source)
{
    User *node = malloc(sizeof(User));

    if (node != NULL) {
        *node = *source;
        node->next = NULL;
        if (db->users == NULL) {
            db->users = node;
        } else {
            User *last = db->users;

            while (last->next != NULL) {
                last = last->next;
            }
            last->next = node;
        }
    }
    return node;
}

/* A password needs 8 to 63 characters with at least one letter and one digit. */
static int password_is_strong(const char *password)
{
    int length = (int)strlen(password);
    int has_letter = 0;
    int has_digit = 0;
    int i;

    for (i = 0; i < length; i++) {
        char c = password[i];

        if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'))) {
            has_letter = 1;
        }
        if ((c >= '0') && (c <= '9')) {
            has_digit = 1;
        }
    }
    return ((length >= 8) && (length < PASSWORD_LEN) && (has_letter == 1) && (has_digit == 1)) ? 1 : 0;
}

/* Scrambles password + salt into a fixed-size hash. */
static int make_hash(const char *password, const unsigned char *salt, unsigned char *hash)
{
    int result = PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT_SIZE,
                                   HASH_ROUNDS, EVP_sha256(), HASH_SIZE, hash);

    return (result == 1) ? 1 : 0;
}

static int check_password(const User *user, const char *password)
{
    unsigned char hash[HASH_SIZE];
    int correct = 0;

    if (make_hash(password, user->salt, hash) != 0) {
        correct = (CRYPTO_memcmp(hash, user->hash, HASH_SIZE) == 0) ? 1 : 0;
    }
    return correct;
}

User *authenticate_user(const Database *db, const char *username, const char *password)
{
    User *user = find_user(db, username);

    if ((user != NULL) && (check_password(user, password) == 0)) {
        user = NULL;
    }
    return user;
}

Result create_user(Database *db, const char *username, const char *password)
{
    Result result = RESULT_OK;
    User user = {0};

    if (find_user(db, username) != NULL) {
        result = RESULT_EXISTS;
    } else if ((password_is_strong(password) == 0) ||
               (copy_text(user.username, USERNAME_LEN, username) == 0)) {
        result = RESULT_BAD_INPUT;
    } else if ((RAND_bytes(user.salt, SALT_SIZE) != 1) ||
               (make_hash(password, user.salt, user.hash) == 0)) {
        result = RESULT_FAILED;
    } else if (append_user(db, &user) == NULL) {
        result = RESULT_NO_MEMORY;
    } else {
        result = RESULT_OK;
    }
    return result;
}

Result delete_user(Database *db, const char *username)
{
    Result result = RESULT_NOT_FOUND;
    User **link = &db->users;

    if (username == NULL) {
        result = RESULT_BAD_INPUT;
    } else {
        while ((*link != NULL) && (strcmp((*link)->username, username) != 0)) {
            link = &(*link)->next;
        }
        if (*link != NULL) {
            User *removed = *link;

            *link = removed->next;
            free(removed);
            result = RESULT_OK;
        }
    }
    return result;
}

Result change_password(User *user, const char *old_password, const char *new_password)
{
    Result result = RESULT_OK;
    unsigned char new_salt[SALT_SIZE];
    unsigned char new_hash[HASH_SIZE];

    if (check_password(user, old_password) == 0) {
        result = RESULT_BAD_INPUT;
    } else if ((password_is_strong(new_password) == 0) || (strcmp(old_password, new_password) == 0)) {
        result = RESULT_BAD_INPUT;
    } else if ((RAND_bytes(new_salt, SALT_SIZE) != 1) ||
               (make_hash(new_password, new_salt, new_hash) == 0)) {
        result = RESULT_FAILED;
    } else {
        (void)memcpy(user->salt, new_salt, SALT_SIZE);
        (void)memcpy(user->hash, new_hash, HASH_SIZE);
    }
    return result;
}
