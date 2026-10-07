/* SPDX-License-Identifier: MIT */

#include "pwf/pwf_stream.h"
#include "pwf_test.h"

static void noop_data_cb(struct pwf_stream* stream, const struct pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

int main(void)
{
    struct pwf_stream* stream = pwf_stream_create(PWF_DATA_AUDIO, noop_data_cb, NULL);
    PWF_ASSERT(stream != NULL);

    /* A NULL config is rejected. */
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, NULL), PWF_ERR_INVALID_ARG);

    /* Invalid formats are rejected without starting the stream. */
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 0, .channels = 2 }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 0 }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = -1, .channels = 2 }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2, .format = "NOT_A_FORMAT" }), PWF_ERR_INVALID_FORMAT);
    PWF_ASSERT_EQ(pwf_stream_start(stream), PWF_ERR_NOT_CONFIGURED);

    /* A valid config is accepted, whether it leaves format NULL (defaults
     * to "S16") or names a supported sample format explicitly. */
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2 }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2, .format = "F32" }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2, .format = "U8" }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(stream, &(struct pwf_audio_config){ .sample_rate = 48000, .channels = 2, .format = "S24_32" }), PWF_OK);

    pwf_stream_destroy(stream);
    return 0;
}
