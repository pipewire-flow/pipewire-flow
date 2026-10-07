/* SPDX-License-Identifier: MIT */

/* Whitebox test: calls the internal per-level emitters directly to
 * check level filtering and callback delivery without needing to
 * trigger a real PipeWire failure. */

#include <stdint.h>
#include <string.h>

#include "pwf_log_internal.h"
#include "pwf_test.h"

static int g_calls = 0;
static pwf_log_level g_last_level;
static int g_last_line;
static char g_last_file[256];
static char g_last_message[512];

static void capture_cb(pwf_log_level level, const char* file, int line, const char* message, void* user_data)
{
    PWF_ASSERT_EQ((intptr_t)user_data, (intptr_t)0x1234);
    g_calls++;
    g_last_level = level;
    g_last_line = line;
    strncpy(g_last_file, file, sizeof(g_last_file) - 1);
    g_last_file[sizeof(g_last_file) - 1] = '\0';
    strncpy(g_last_message, message, sizeof(g_last_message) - 1);
    g_last_message[sizeof(g_last_message) - 1] = '\0';
}

int main(void)
{
    pwf_log_set_callback(capture_cb, (void*)0x1234);

    /* Default level is PWF_LOG_WARNING: error/warning pass, the rest don't. */
    g_calls = 0;
    pwf_log_error("err %d", 1);
    PWF_ASSERT_EQ(g_calls, 1);
    PWF_ASSERT_EQ(g_last_level, PWF_LOG_ERROR);
    PWF_ASSERT(strcmp(g_last_message, "err 1") == 0);
    PWF_ASSERT(strcmp(g_last_file, "test_log.c") == 0);
    PWF_ASSERT(g_last_line > 0);

    g_calls = 0;
    pwf_log_warning("warning message");
    PWF_ASSERT_EQ(g_calls, 1);
    PWF_ASSERT_EQ(g_last_level, PWF_LOG_WARNING);

    g_calls = 0;
    pwf_log_info("info message");
    PWF_ASSERT_EQ(g_calls, 0);

    g_calls = 0;
    pwf_log_debug("debug message");
    PWF_ASSERT_EQ(g_calls, 0);

    g_calls = 0;
    pwf_log_verbose("verbose message");
    PWF_ASSERT_EQ(g_calls, 0);

    /* Raising the level lets less severe messages through too. */
    pwf_log_set_level(PWF_LOG_VERBOSE);
    g_calls = 0;
    pwf_log_verbose("now visible");
    PWF_ASSERT_EQ(g_calls, 1);
    PWF_ASSERT(strcmp(g_last_message, "now visible") == 0);

    /* Clearing the callback stops delivery (falls back to stderr). */
    pwf_log_set_callback(NULL, NULL);
    g_calls = 0;
    pwf_log_error("no callback registered");
    PWF_ASSERT_EQ(g_calls, 0);

    return 0;
}
