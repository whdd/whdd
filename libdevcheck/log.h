#ifndef LIBDEVCHECK_LOG_H
#define LIBDEVCHECK_LOG_H

#include <stdarg.h>

enum DC_LogLevel {
    DC_LOG_QUIET = -1,
    DC_LOG_PANIC = 0, // going to crash
    DC_LOG_FATAL, // cannot proceed current procedure
    DC_LOG_ERROR, // sth went wrong, result will not be perfect, but it will be given
    DC_LOG_WARNING, // sth may or may not be wrong
    DC_LOG_INFO, // procedure reports interesting for end user
    DC_LOG_DEBUG, // reports for developer during debugging
};
enum DC_LogLevel dc_log_get_level(void);
void dc_log_set_level(enum DC_LogLevel level);
void dc_log_set_callback(void (*log_func)(void *priv, enum DC_LogLevel level, const char* fmt, va_list vl), void *logger_priv);
char *log_level_name(enum DC_LogLevel level);
void dc_log_default_func(void *priv, enum DC_LogLevel level, const char* fmt, va_list vl);
char *dc_log_default_form_string(enum DC_LogLevel level, const char* fmt, va_list vl);

void dc_log(enum DC_LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

#endif // LIBDEVCHECK_LOG_H
