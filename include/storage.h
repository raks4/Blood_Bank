/* storage.h - saving to and loading from the text files (see storage.c). */
#ifndef STORAGE_H
#define STORAGE_H

#include "types.h"

int         load_all(Database *db);
int         save_all(const Database *db);

#endif
