/* SPDX-License-Identifier: MIT */

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pwf_log_internal.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pwf_log_func_t g_callback = NULL;
static void* g_user_data = NULL;
static enum pwf_log_level g_level = PWF_LOG_WARNING;

void pwf_log_set_callback(pwf_log_func_t callback, void* user_data)
{
    pthread_mutex_lock(&g_lock);
    g_callback = callback;
    g_user_data = user_data;
    pthread_mutex_unlock(&g_lock);
}

void pwf_log_set_level(enum pwf_log_level level)
{
    pthread_mutex_lock(&g_lock);
    g_level = level;
    pthread_mutex_unlock(&g_lock);
}

static const char* pwf_log_level_name(enum pwf_log_level level)
{
    switch (level) {
    case PWF_LOG_ERROR:   return "error";
    case PWF_LOG_WARNING: return "warning";
    case PWF_LOG_INFO:    return "info";
    case PWF_LOG_DEBUG:   return "debug";
    case PWF_LOG_VERBOSE: return "verbose";
    default:              return "?";
    }
}

/* __FILE__ carries whatever path the compiler was invoked with (often
 * a long build-relative path); trim it to the basename to match
 * PipeWire's short "loop.c:67"-style output. */
static const char* pwf_log_basename(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void pwf_log_emit(enum pwf_log_level level, const char* file, int line, const char* fmt, ...)
{
    pthread_mutex_lock(&g_lock);
    bool pass = level <= g_level;
    pwf_log_func_t callback = g_callback;
    void* user_data = g_user_data;
    pthread_mutex_unlock(&g_lock);

    if (!pass)
        return;

    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    const char* base = pwf_log_basename(file);

    if (callback)
        callback(level, base, line, message, user_data);
    else
        fprintf(stderr, "[pwf:%s] %s:%d: %s\n", pwf_log_level_name(level), base, line, message);
}
