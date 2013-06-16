#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "libdevcheck.h"
#include "log.h"

enum DC_LogLevel dc_log_get_level(void) {
    return dc_ctx_global->log_level;
}
void dc_log_set_level(enum DC_LogLevel level) {
    dc_ctx_global->log_level = level;
}
void dc_log_set_callback(void (*log_func)(void *priv, enum DC_LogLevel level, const char* fmt, va_list vl), void *logger_priv) {
    dc_ctx_global->log_func = log_func;
    dc_ctx_global->logger_priv = logger_priv;
}

char *log_level_name(enum DC_LogLevel level) {
    switch (level) {
        case DC_LOG_PANIC:   return "PANIC";
        case DC_LOG_FATAL:   return "FATAL";
        case DC_LOG_ERROR:   return "ERROR";
        case DC_LOG_WARNING: return "WARNING";
        case DC_LOG_INFO:    return "INFO";
        case DC_LOG_DEBUG:   return "DEBUG";
        default: return "UNKNOWN";
    }
}
char *dc_log_default_form_string(enum DC_LogLevel level, const char* fmt, va_list vl) {
    int r;
    char *msg;
    char *ret;
    r = vasprintf(&msg, fmt, vl);
    if (r == -1) {
        fprintf(stderr, "Forming log string fail\n");
        return NULL;
    }
    r = asprintf(&ret, "[%s] %s", log_level_name(level), msg);
    free(msg);
    if (r == -1) {
        fprintf(stderr, "Forming log string fail\n");
        return NULL;
    }
    return ret;
}

void dc_log_default_func(void *priv, enum DC_LogLevel level, const char* fmt, va_list vl) {
    (void)priv;
    char *msg = dc_log_default_form_string(level, fmt, vl);
    fprintf(stderr, "%s", msg);
    free(msg);
}

void dc_log(enum DC_LogLevel level, const char* fmt, ...) {
    if (!dc_ctx_global->log_func)
        return;
    va_list vl;
    va_start(vl, fmt);
    if (level <= dc_ctx_global->log_level)
        dc_ctx_global->log_func(dc_ctx_global->logger_priv, level, fmt, vl);
    va_end(vl);
}
