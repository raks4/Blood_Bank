/* utils.h - safe keyboard input, dates and small helper functions (see utils.c). */
#ifndef UTILS_H
#define UTILS_H

#include "types.h"

int         read_input(const char *prompt, char *buffer, int size, int hidden);
int         ask_input(const char *prompt, InputKind kind, char *text, int size,
                      int *number, int min, int max);
long        today(void);
int         current_year(void);

#endif
