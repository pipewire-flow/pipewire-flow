/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "pwf/pwf_filter.h"
#include "pwf_test.h"

static int g_cycles = 0;
static size_t g_last_n_buffers = 0;

static void process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                       void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)user_data;
    g_cycles++;
    g_last_n_buffers = n_buffers;
}

int main(void)
{
    struct pwf_filter* filter = pwf_filter_create("pwf-test-signal", process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    /* A signal port needs no config; both directions are valid. */
    struct pwf_filter_port* sig_in = pwf_filter_add_signal_port(filter, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(sig_in != NULL);
    struct pwf_filter_port* audio_in = pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT,
                                                                   &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2 });
    PWF_ASSERT(audio_in != NULL);

    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    /* Pushing data into a signal input port works through the same
     * generic staging mechanism as any other port kind. */
    float samples[4] = { 0.0f, 0.25f, 0.5f, 0.75f };
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, sig_in, samples, sizeof(samples), -1), PWF_OK);

    sleep(1);

    /* Both ports' buffers must arrive together, every cycle. */
    PWF_ASSERT(g_cycles > 0);
    PWF_ASSERT_EQ(g_last_n_buffers, (size_t)2);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);

    /* Adding a signal port after the filter has started is rejected. */
    struct pwf_filter* started = pwf_filter_create("pwf-test-signal-started", process_cb, NULL);
    PWF_ASSERT(started != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(started, PWF_FILTER_PORT_INPUT,
                                          &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2 }) != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(started), PWF_OK);
    PWF_ASSERT(pwf_filter_add_signal_port(started, PWF_FILTER_PORT_OUTPUT) == NULL);
    pwf_filter_stop(started, false);
    pwf_filter_destroy(started);

    return 0;
}
