/* donors.h - the linked list of donors (see donors.c). */
#ifndef DONORS_H
#define DONORS_H

#include "types.h"

Donor      *find_donor(const Database *db, int id);
Donor      *append_donor(Database *db, const Donor *source);
Result      add_donor(Database *db, const char *name, const char *phone, int group,
                      int birth_year, int *new_id);
Result      deactivate_donor(Database *db, int id);
Result      delete_donor(Database *db, int id);
int         update_donor_statuses(Database *db, long now);
const char *donor_problem(const Donor *donor, long now);
void        list_donors(const Database *db, int group);

#endif
