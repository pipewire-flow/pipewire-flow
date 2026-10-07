/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "pwf/pwf_filter.h"
#include "pwf_test.h"

static int g_calls = 0;
static size_t g_last_size = 0;
static int64_t g_last_pts = 0;

static void process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    (void)user_data;
    for (size_t i = 0; i < n_buffers; i++) {
        if (buffers[i].data) {
            g_calls++;
            g_last_size = buffers[i].size;
            g_last_pts = buffers[i].pts;
        }
    }
}

int main(void)
{
    pwf_filter_h filter = pwf_filter_create("pwf-test-push", process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };
    pwf_filter_port_h in_port = pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT, &cfg);
    pwf_filter_port_h out_port = pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_OUTPUT, &cfg);
    PWF_ASSERT(in_port != NULL);
    PWF_ASSERT(out_port != NULL);

    /* Pushing to an output port is rejected. */
    char dummy[4] = { 0 };
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, out_port, dummy, sizeof(dummy), -1), PWF_ERR_INVALID_ARG);

    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    /* Only the most recently pushed buffer per port is kept. */
    char first[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    char second[16] = { 0 };
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, in_port, first, sizeof(first), 1000), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, in_port, second, sizeof(second), 2000), PWF_OK);

    sleep(1);

    PWF_ASSERT(g_calls > 0);
    PWF_ASSERT_EQ(g_last_size, sizeof(second));
    PWF_ASSERT_EQ(g_last_pts, (int64_t)2000);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);
    return 0;
}
