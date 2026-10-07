/* SPDX-License-Identifier: MIT */

#include "pwf/pwf_stream.h"
#include "pwf_test.h"

static void noop_data_cb(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

int main(void)
{
    pwf_stream_h stream = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
    PWF_ASSERT(stream != NULL);

    /* A NULL config is rejected. */
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, NULL), PWF_ERR_INVALID_ARG);

    /* Invalid dimensions, unrecognized pixel formats, and negative fps are rejected. */
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, &(pwf_video_config){ .width = 0, .height = 480, .pixel_format = "RGB" }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, &(pwf_video_config){ .width = 640, .height = -1, .pixel_format = "RGB" }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = "NOT_A_FORMAT" }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = "RGB", .fps = -1 }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_start(stream), PWF_ERR_NOT_CONFIGURED);

    /* A valid config (with an explicit frame rate) is accepted. */
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 }), PWF_OK);

    pwf_stream_destroy(stream);

    /* Every supported pixel format is recognized. */
    static const char* supported_formats[] = { "RGB", "YUYV", "NV12", "NV21", "I420", "MJPG", "H264" };
    for (size_t i = 0; i < sizeof(supported_formats) / sizeof(supported_formats[0]); i++) {
        pwf_stream_h s = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
        PWF_ASSERT(s != NULL);
        PWF_ASSERT_EQ(pwf_stream_set_video_config(s, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = supported_formats[i], .fps = 30 }), PWF_OK);
        pwf_stream_destroy(s);
    }

    /* Only the FourCC spelling "MJPG" is recognized, not "MJPEG". */
    pwf_stream_h mjpeg_spelling = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
    PWF_ASSERT(mjpeg_spelling != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_video_config(mjpeg_spelling, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = "MJPEG", .fps = 30 }),
                  PWF_ERR_INVALID_FORMAT);
    pwf_stream_destroy(mjpeg_spelling);

    return 0;
}
