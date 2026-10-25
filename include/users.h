/* users.h - login accounts and passwords (see users.c). */
#ifndef USERS_H
#define USERS_H

#include "types.h"

User       *find_user(const Database *db, const char *username);
User       *append_user(Database *db, const User *source);
Result      create_user(Database *db, const char *username, const char *password);
int         check_password(const User *user, const char *password);
Result      change_password(User *user, const char *old_password, const char *new_password);

#endif
