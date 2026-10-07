/* SPDX-License-Identifier: MIT */

#include <string.h>

#include "pwf/pwf_filter.h"
#include "pwf_filter_internal.h"
#include "pwf_test.h"

static void noop_process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                             void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

int main(void)
{
    /* Starting with zero ports is rejected. */
    struct pwf_filter* empty = pwf_filter_create("pwf-test-empty", noop_process_cb, NULL);
    PWF_ASSERT(empty != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(empty), PWF_ERR_NOT_CONFIGURED);
    pwf_filter_destroy(empty);

    struct pwf_filter* filter = pwf_filter_create("pwf-test-filter", noop_process_cb, NULL);
    PWF_ASSERT(filter != NULL);
    /* The name given at creation must be the one used as the underlying
     * PipeWire node's identifying property (whitebox check). */
    PWF_ASSERT(strcmp(((struct pwf_filter*)filter)->name, "pwf-test-filter") == 0);

    /* Invalid audio/video configs are rejected (NULL handle returned). */
    struct pwf_audio_config bad_audio = { .sample_rate = 0, .channels = 2 };
    PWF_ASSERT(pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT, &bad_audio) == NULL);

    struct pwf_audio_config bad_audio_format = { .sample_rate = 48000, .channels = 2, .format = "NOT_A_FORMAT" };
    PWF_ASSERT(pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT, &bad_audio_format) == NULL);

    struct pwf_video_config bad_video = { .width = 640, .height = 480, .pixel_format = "NOT_A_FORMAT", .fps = 0 };
    PWF_ASSERT(pwf_filter_add_video_port(filter, PWF_FILTER_PORT_INPUT, &bad_video) == NULL);

    /* A filter with only input ports is valid and can start. */
    struct pwf_audio_config audio_cfg = { .sample_rate = 48000, .channels = 2 };
    struct pwf_filter_port* in_port = pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT, &audio_cfg);
    PWF_ASSERT(in_port != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    /* Adding a port after the filter has started is rejected. */
    PWF_ASSERT(pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_OUTPUT, &audio_cfg) == NULL);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);

    /* A filter with only output ports is also valid and can start. */
    struct pwf_filter* out_only = pwf_filter_create("pwf-test-out-only", noop_process_cb, NULL);
    PWF_ASSERT(out_only != NULL);
    struct pwf_video_config video_cfg = { .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 };
    PWF_ASSERT(pwf_filter_add_video_port(out_only, PWF_FILTER_PORT_OUTPUT, &video_cfg) != NULL);

    /* MJPEG is accepted on a filter video port the same way a raw format is. */
    struct pwf_video_config mjpg_cfg = { .width = 640, .height = 480, .pixel_format = "MJPG", .fps = 30 };
    PWF_ASSERT(pwf_filter_add_video_port(out_only, PWF_FILTER_PORT_OUTPUT, &mjpg_cfg) != NULL);

    PWF_ASSERT_EQ(pwf_filter_start(out_only), PWF_OK);
    pwf_filter_destroy(out_only);

    return 0;
}
