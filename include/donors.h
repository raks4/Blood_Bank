/* donors.h - the linked list of donors (see donors.c). */
#ifndef DONORS_H
#define DONORS_H

#include "types.h"

Donor      *find_donor(const Database *db, int id);
Result      add_donor(Database *db, const char *name, const char *phone, int group,
                      int birth_year, int *new_id);
Result      update_donor(Database *db, int id, DonorAction action);
int         update_donor_statuses(Database *db, long now);
const char *donor_problem(const Donor *donor, long now);
void        list_donors(const Database *db, int group);

#endif
