/* SPDX-License-Identifier: MIT */

#ifndef PWF_LOG_H
#define PWF_LOG_H

#include "pwf/pwf_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Severity of one log message, most to least severe. */
typedef enum {
    PWF_LOG_ERROR   = 0,
    PWF_LOG_WARNING = 1,
    PWF_LOG_INFO    = 2,
    PWF_LOG_DEBUG   = 3,
    PWF_LOG_VERBOSE = 4
} pwf_log_level;

/* Receives one formatted message tagged with the logging file's basename and line, both valid only for
 * this call. It can run on any thread, a real-time data thread included, so it must not block. */
typedef void (*pwf_log_cb)(pwf_log_level level, const char* file, int line, const char* message, void* user_data);

/* Registers (or clears, with NULL) the process-wide log callback,
 * replacing whatever was set before. With no callback set, messages
 * are written to stderr instead. */
PWF_API void pwf_log_set_callback(pwf_log_cb callback, void* user_data);

/* Sets the minimum severity delivered to the callback (or stderr);
 * messages less severe than `level` are dropped before formatting.
 * Default: PWF_LOG_WARNING. */
PWF_API void pwf_log_set_level(pwf_log_level level);

#ifdef __cplusplus
}
#endif

#endif /* PWF_LOG_H */
