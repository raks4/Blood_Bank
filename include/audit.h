/* audit.h - timestamped audit events (see audit.c). */
#ifndef AUDIT_H
#define AUDIT_H

int audit_log_event(const char *actor, const char *action);
int audit_logger_start(void);
void audit_logger_stop(void);

#endif
