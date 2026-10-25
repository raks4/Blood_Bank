/* blood.h - batches (donations), stock and hospital requests (see blood.c). */
#ifndef BLOOD_H
#define BLOOD_H

#include "types.h"

Batch      *find_batch(const Database *db, int id);
Batch      *append_batch(Database *db, const Batch *source);
Result      record_donation(Database *db, Donor *donor, int ml, long now, int *batch_id);
Result      mark_batch_tested(Database *db, int id, long now);
Result      discard_batch(Database *db, int id);
int         stock_for_group(const Database *db, int group, long now);
void        show_stock(const Database *db, long now);
void        list_batches(const Database *db, int donor_id, long now);

Request    *find_request(const Database *db, int id);
Request    *append_request(Database *db, const Request *source);
Result      add_request(Database *db, const char *hospital, int group, int ml,
                        long now, int *new_id);
Result      process_request(Database *db, int id, long now);
Result      reject_request(Database *db, int id);
void        list_requests(const Database *db);
void        free_database(Database *db);

#endif
