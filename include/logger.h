#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* use dprintf to write direct to fd without buffering */
#define log_print(...) dprintf(1, __VA_ARGS__)
#define log_info(...) dprintf(1, "[INFO] " __VA_ARGS__)
#define log_warn(...) dprintf(2, "[WARNING] " __VA_ARGS__)
#define log_err(...) dprintf(2, "[ERROR] " __VA_ARGS__)
#define log_fatal(...) dprintf(2, "[FATAL] " __VA_ARGS__)

#ifdef DEBUG
#define log_debug(...) dprintf(1, "[DEBUG] " __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif

#endif
