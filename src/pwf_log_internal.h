/* SPDX-License-Identifier: MIT */

#ifndef PWF_LOG_INTERNAL_H
#define PWF_LOG_INTERNAL_H

#include "pwf/pwf_log.h"

#if defined(__GNUC__) || defined(__clang__)
#define PWF_LOG_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define PWF_LOG_PRINTF_FMT(fmt_idx, args_idx)
#endif

/* Formats and delivers one message through the registered callback
 * (or stderr, if none is set) at `level`, tagged with the call
 * site's file/line; dropped if less severe than the configured
 * minimum level. Called through the pwf_log_<level>() macros below,
 * which supply __FILE__/__LINE__ automatically. */
void pwf_log_emit(enum pwf_log_level level, const char* file, int line, const char* fmt, ...) PWF_LOG_PRINTF_FMT(4, 5);

/* Mirrors PipeWire's own pw_log_error()/pw_log_warn()/... naming. */
#define pwf_log_error(...) pwf_log_emit(PWF_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define pwf_log_warning(...) pwf_log_emit(PWF_LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define pwf_log_info(...) pwf_log_emit(PWF_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define pwf_log_debug(...) pwf_log_emit(PWF_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define pwf_log_verbose(...) pwf_log_emit(PWF_LOG_VERBOSE, __FILE__, __LINE__, __VA_ARGS__)

#endif /* PWF_LOG_INTERNAL_H */
