/* SPDX-License-Identifier: MIT */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "pwf/pwf_stream.h"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void on_data(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    /* MJPEG frames are compressed, so this size varies frame to frame
     * unlike a raw pixel format's fixed size. */
    printf("video: received MJPEG frame of %zu bytes (pts=%lld ns)\n", buf->size, (long long)buf->pts);
}

static void on_error(pwf_stream_h stream, int error_code, void* user_data)
{
    (void)stream;
    (void)user_data;
    fprintf(stderr, "video: source lost (error %d)\n", error_code);
    g_running = 0;
}

int main(void)
{
    signal(SIGINT, on_signal);

    pwf_stream_h stream = pwf_stream_create(PWF_DATA_VIDEO, on_data, NULL);
    if (!stream) {
        fprintf(stderr, "failed to create video stream (is PipeWire running?)\n");
        return 1;
    }

    pwf_stream_set_error_cb(stream, on_error);

    pwf_video_config cfg = { .width = 1280, .height = 720, .pixel_format = "MJPG", .fps = 30 };
    if (pwf_stream_set_video_config(stream, &cfg) != PWF_OK) {
        fprintf(stderr, "failed to set video format (camera may not offer MJPEG at this size)\n");
        pwf_stream_destroy(stream);
        return 1;
    }

    if (pwf_stream_start(stream) != PWF_OK) {
        fprintf(stderr, "failed to start video stream\n");
        pwf_stream_destroy(stream);
        return 1;
    }

    printf("capturing MJPEG video, press Ctrl+C to stop...\n");
    while (g_running)
        sleep(1);

    pwf_stream_stop(stream, false);
    pwf_stream_destroy(stream);
    return 0;
}
