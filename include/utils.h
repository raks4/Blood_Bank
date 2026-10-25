/* utils.h - safe keyboard input, dates and small helper functions (see utils.c). */
#ifndef UTILS_H
#define UTILS_H

#include "types.h"

int         input_closed(void);
int         read_line(const char *prompt, char *buffer, int size);
int         read_password(const char *prompt, char *buffer, int size);
int         ask_text(const char *prompt, char *buffer, int size);
int         ask_phone(const char *prompt, char *buffer, int size);
int         ask_number(const char *prompt, int min, int max, int *value);
int         ask_group(const char *prompt, int *group);
int         ask_yes(const char *prompt, int *yes);
int         copy_text(char *destination, int size, const char *source);
void        wipe(char *buffer, int size);
const char *group_name(int group);
long        today(void);
int         current_year(void);
void        format_date(long when, char *buffer, int size);
int         hex_to_bytes(const char *hex, unsigned char *bytes, int count);
const char *result_text(Result result);

#endif
