/* SPDX-License-Identifier: MIT */

/* Measures what tinypipewire costs a process around its streams' lifetime:
 * before the first stream exists, while one streams, and after the last one
 * is destroyed. The process reads its own /proc entries, so each figure is
 * taken at exactly that point. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bench_common.h"
#include "tpw/tpw_stream.h"

static void on_data(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

/* Pss in KiB and the number of distinct shared objects mapped, split into
 * all of them and the PipeWire ones (libpipewire, its modules, SPA plugins). */
static void report(const char* phase)
{
    char path[64];
    char line[512];
    long pss = -1;

    snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", getpid());
    FILE* f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "Pss: %ld", &pss) == 1)
                break;
        }
        fclose(f);
    }

    int sos = 0;
    int pw_sos = 0;
    snprintf(line, sizeof(line), "grep -o '/[^ ]*\\.so[^ ]*' /proc/%d/maps | sort -u", getpid());
    f = popen(line, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            sos++;
            if (strstr(line, "pipewire-0.3") || strstr(line, "/spa-0.2/"))
                pw_sos++;
        }
        pclose(f);
    }

    printf("| %s | %ld | %d | %d |\n", phase, pss, sos, pw_sos);
}

int main(int argc, char** argv)
{
    bench_options opts;
    int res = bench_parse_options(argc, argv, false, &opts);
    if (res != 0)
        return res < 0 ? 0 : res;
    int seconds = opts.duration > 0 ? opts.duration : 2;

    printf("| phase (%s) | PSS KiB | .so mapped | PipeWire .so |\n", opts.video ? "video" : "audio");
    printf("| --- | ---: | ---: | ---: |\n");
    report("before the first stream");

    tpw_stream_h stream = tpw_stream_create(opts.video ? TPW_DATA_VIDEO : TPW_DATA_AUDIO, on_data, NULL);
    if (!stream) {
        fprintf(stderr, "bench_idle: failed to create a stream (is PipeWire running?)\n");
        return 1;
    }

    int cfg_res;
    if (opts.video) {
        tpw_video_config cfg = { .width = BENCH_VIDEO_WIDTH, .height = BENCH_VIDEO_HEIGHT, .pixel_format = "YUYV" };
        cfg_res = tpw_stream_set_video_config(stream, &cfg);
    } else {
        tpw_audio_config cfg = { .sample_rate = BENCH_AUDIO_RATE, .channels = BENCH_AUDIO_CHANNELS };
        cfg_res = tpw_stream_set_audio_config(stream, &cfg);
    }
    if (cfg_res != TPW_OK || tpw_stream_start(stream) != TPW_OK) {
        fprintf(stderr, "bench_idle: failed to start the stream\n");
        tpw_stream_destroy(stream);
        return 1;
    }

    sleep((unsigned)seconds);
    report("streaming");

    tpw_stream_stop(stream, false);
    tpw_stream_destroy(stream);
    report("after the last stream is destroyed");
    return 0;
}
