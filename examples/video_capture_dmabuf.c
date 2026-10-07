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
    (void)user_data;

    pwf_dmabuf_plane planes[4];
    size_t n_planes = pwf_stream_get_dmabuf_planes(stream, planes, 4);
    if (n_planes == 0) {
        printf("video: frame with no DMABUF plane (pts=%lld ns)\n", (long long)buf->pts);
        return;
    }

    printf("video: %zu plane(s), fd=%d stride=%u size=%u (pts=%lld ns)\n", n_planes, planes[0].fd,
           planes[0].stride, planes[0].size, (long long)buf->pts);
}

static void on_error(pwf_stream_h stream, int error_code, void* user_data)
{
    (void)stream;
    (void)user_data;
    fprintf(stderr, "video: source lost or could not negotiate DMABUF (error %d)\n", error_code);
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

    pwf_video_config cfg = { .width = 640, .height = 480, .pixel_format = "YUYV", .fps = 30 };
    pwf_stream_dmabuf_opts opts = { .memory = PWF_PORT_MEMORY_DMABUF };
    if (pwf_stream_set_video_config_ex(stream, &cfg, &opts) != PWF_OK) {
        fprintf(stderr, "failed to request DMABUF video format\n");
        pwf_stream_destroy(stream);
        return 1;
    }

    if (pwf_stream_start(stream) != PWF_OK) {
        fprintf(stderr, "failed to start video stream\n");
        pwf_stream_destroy(stream);
        return 1;
    }

    printf("capturing video via DMABUF, press Ctrl+C to stop...\n");
    while (g_running)
        sleep(1);

    pwf_stream_stop(stream, false);
    pwf_stream_destroy(stream);
    return 0;
}
