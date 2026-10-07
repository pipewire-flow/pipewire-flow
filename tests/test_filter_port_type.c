/* SPDX-License-Identifier: MIT */

#include "pwf/pwf_filter.h"
#include "pwf_test.h"

static void noop_process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers,
                             void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

int main(void)
{
    pwf_filter_h filter = pwf_filter_create("pwf-test-port-type", noop_process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    pwf_filter_port_h audio = pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT,
                                                          &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 });
    pwf_filter_port_h video = pwf_filter_add_video_port(
        filter, PWF_FILTER_PORT_INPUT, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 });
    pwf_filter_port_h signal = pwf_filter_add_signal_port(filter, PWF_FILTER_PORT_INPUT);
    pwf_filter_port_h event = pwf_filter_add_event_port(filter, PWF_FILTER_PORT_INPUT);

    PWF_ASSERT(audio != NULL);
    PWF_ASSERT(video != NULL);
    PWF_ASSERT(signal != NULL);
    PWF_ASSERT(event != NULL);

    PWF_ASSERT_EQ(pwf_filter_port_get_type(audio), PWF_DATA_AUDIO);
    PWF_ASSERT_EQ(pwf_filter_port_get_type(video), PWF_DATA_VIDEO);
    PWF_ASSERT_EQ(pwf_filter_port_get_type(signal), PWF_DATA_SIGNAL);
    PWF_ASSERT_EQ(pwf_filter_port_get_type(event), PWF_DATA_EVENT);

    pwf_filter_destroy(filter);
    return 0;
}
