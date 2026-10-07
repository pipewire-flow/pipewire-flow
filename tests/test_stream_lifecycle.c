/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "pwf/pwf_stream.h"
#include "pwf_test.h"

static int g_data_calls = 0;

static void count_data_cb(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
    g_data_calls++;
}

int main(void)
{
    /* start() before a format is set must be rejected. */
    pwf_stream_h s1 = pwf_stream_create(PWF_DATA_AUDIO, count_data_cb, NULL);
    PWF_ASSERT(s1 != NULL);
    PWF_ASSERT_EQ(pwf_stream_start(s1), PWF_ERR_NOT_CONFIGURED);
    pwf_stream_destroy(s1);

    /* Full lifecycle: create -> set format -> start -> stop -> restart -> destroy. */
    pwf_stream_h s2 = pwf_stream_create(PWF_DATA_AUDIO, count_data_cb, NULL);
    PWF_ASSERT(s2 != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s2, &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 }), PWF_OK);

    PWF_ASSERT_EQ(pwf_stream_start(s2), PWF_OK);
    sleep(1);
    PWF_ASSERT_EQ(pwf_stream_stop(s2, false), PWF_OK);
    int calls_after_stop = g_data_calls;
    sleep(1);
    PWF_ASSERT_EQ(g_data_calls, calls_after_stop); /* no delivery once stopped */

    PWF_ASSERT_EQ(pwf_stream_start(s2), PWF_OK); /* restart after stop */
    sleep(1);
    PWF_ASSERT_EQ(pwf_stream_stop(s2, false), PWF_OK);
    pwf_stream_destroy(s2);

    /* destroy() while running must stop delivery and release resources safely. */
    pwf_stream_h s3 = pwf_stream_create(PWF_DATA_AUDIO, count_data_cb, NULL);
    PWF_ASSERT(s3 != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s3, &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s3), PWF_OK);
    pwf_stream_destroy(s3);

    /* One audio stream and one video stream running concurrently; stopping
     * or destroying one must not affect the other. */
    pwf_stream_h audio = pwf_stream_create(PWF_DATA_AUDIO, count_data_cb, NULL);
    pwf_stream_h video = pwf_stream_create(PWF_DATA_VIDEO, count_data_cb, NULL);
    PWF_ASSERT(audio != NULL);
    PWF_ASSERT(video != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(audio, &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_video_config(video, &(pwf_video_config){ .width = 640, .height = 480, .pixel_format = "RGB" }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(audio), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(video), PWF_OK);
    sleep(1);

    pwf_stream_destroy(audio);
    sleep(1);
    PWF_ASSERT_EQ(pwf_stream_stop(video, false), PWF_OK); /* video unaffected by audio's destroy */
    pwf_stream_destroy(video);

    return 0;
}
