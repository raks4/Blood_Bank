/* blood.h - batches (donations), stock and hospital requests (see blood.c). */
#ifndef BLOOD_H
#define BLOOD_H

#include "types.h"

Result      record_donation(Database *db, Donor *donor, int ml, long now, int *batch_id);
Result      update_batch(const Database *db, int id, BatchAction action, long now);
int         expire_batches(Database *db, long now);
int         stock_for_group(const Database *db, int group, long now);
void        show_stock(const Database *db, long now);
void        list_batches(const Database *db, int donor_id, long now);

Result      add_request(Database *db, const char *hospital, int group, int ml,
                        long now, int *new_id);
Result      update_request(const Database *db, int id, RequestAction action, long now);
void        list_requests(const Database *db);
void        free_database(Database *db);

#endif
