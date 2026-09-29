/* SPDX-License-Identifier: MIT */

/* The tinypipewire side of the footprint comparison; bench_raw.c does the
 * same work on pw_stream directly. */

#include <stdatomic.h>
#include <unistd.h>

#include "bench_common.h"
#include "tpw/tpw_stream.h"

static atomic_ulong g_buffers[BENCH_MAX_STREAMS];

static void on_data(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    atomic_fetch_add_explicit(&g_buffers[(size_t)user_data], 1, memory_order_relaxed);
}

static void on_error(tpw_stream_h stream, int error_code, void* user_data)
{
    (void)stream;
    fprintf(stderr, "bench_tpw: stream %zu lost its source (error %d)\n", (size_t)user_data, error_code);
}

int main(int argc, char** argv)
{
    bench_options opts;
    int res = bench_parse_options(argc, argv, false, &opts);
    if (res != 0)
        return res < 0 ? 0 : res;

    tpw_stream_h streams[BENCH_MAX_STREAMS] = { 0 };
    int ret = 1;

    for (int i = 0; i < opts.n_streams; i++) {
        streams[i] = tpw_stream_create(opts.video ? TPW_DATA_VIDEO : TPW_DATA_AUDIO, on_data, (void*)(size_t)i);
        if (!streams[i]) {
            fprintf(stderr, "bench_tpw: failed to create stream %d (is PipeWire running?)\n", i);
            goto out;
        }
        tpw_stream_set_error_cb(streams[i], on_error);

        int cfg_res;
        if (opts.video) {
            tpw_video_config cfg = { .width = BENCH_VIDEO_WIDTH, .height = BENCH_VIDEO_HEIGHT,
                                     .pixel_format = "YUYV" };
            cfg_res = tpw_stream_set_video_config(streams[i], &cfg);
        } else {
            tpw_audio_config cfg = { .sample_rate = BENCH_AUDIO_RATE, .channels = BENCH_AUDIO_CHANNELS };
            cfg_res = tpw_stream_set_audio_config(streams[i], &cfg);
        }
        if (cfg_res != TPW_OK || tpw_stream_start(streams[i]) != TPW_OK) {
            fprintf(stderr, "bench_tpw: failed to start stream %d\n", i);
            goto out;
        }
    }

    for (int elapsed = 0; g_bench_running && (opts.duration == 0 || elapsed < opts.duration); elapsed++)
        sleep(1);

    ret = 0;
    for (int i = 0; i < opts.n_streams; i++)
        printf("bench_tpw: stream %d received %lu buffers\n", i, atomic_load(&g_buffers[i]));

out:
    for (int i = 0; i < opts.n_streams; i++) {
        if (streams[i]) {
            tpw_stream_stop(streams[i], false);
            tpw_stream_destroy(streams[i]);
        }
    }
    return ret;
}
