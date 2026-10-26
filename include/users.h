/* users.h - login accounts and passwords (see users.c). */
#ifndef USERS_H
#define USERS_H

#include "types.h"

User       *authenticate_user(const Database *db, const char *username, const char *password);
Result      create_user(Database *db, const char *username, const char *password);
Result      change_password(User *user, const char *old_password, const char *new_password);

#endif
