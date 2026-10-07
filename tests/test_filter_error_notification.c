/* SPDX-License-Identifier: MIT */

/* Whitebox test: simulates a port's format being cleared (PipeWire's
 * signal that whatever fed it is gone) by calling the param_changed
 * handler directly, rather than physically removing a device. */

#include "pwf_filter_internal.h"
#include "pwf_test.h"

static int g_error_calls = 0;
static pwf_filter_port_h g_last_port = NULL;
static int g_last_error_code = 0;

static void noop_process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers,
                             void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

static void on_error(pwf_filter_h filter, pwf_filter_port_h port, int error_code, void* user_data)
{
    (void)filter;
    (void)user_data;
    g_error_calls++;
    g_last_port = port;
    g_last_error_code = error_code;
}

int main(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-error", noop_process_cb, NULL);
    PWF_ASSERT(handle != NULL);
    PWF_ASSERT_EQ(pwf_filter_set_error_cb(handle, on_error), PWF_OK);

    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };
    pwf_filter_port_h port_a = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &cfg);
    pwf_filter_port_h port_b = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &cfg);
    PWF_ASSERT(port_a != NULL);
    PWF_ASSERT(port_b != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(handle), PWF_OK);

    struct pwf_filter* filter = (struct pwf_filter*)handle;

    /* port_a loses its format (source gone); port_b is unaffected. */
    pwf_filter_on_param_changed(filter, (struct pwf_filter_port*)port_a, SPA_PARAM_Format, NULL);

    PWF_ASSERT_EQ(g_error_calls, 1);
    PWF_ASSERT(g_last_port == port_a);
    PWF_ASSERT_EQ(g_last_error_code, PWF_ERR_SOURCE_UNAVAILABLE);

    /* An unrelated param id, or a non-NULL format, must not fire the callback. */
    pwf_filter_on_param_changed(filter, (struct pwf_filter_port*)port_b, SPA_PARAM_Props, NULL);
    PWF_ASSERT_EQ(g_error_calls, 1);

    pwf_filter_stop(handle, false);
    pwf_filter_destroy(handle);
    return 0;
}
