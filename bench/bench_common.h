/* SPDX-License-Identifier: MIT */

/* Command line shared by bench_tpw and bench_raw, so both programs run the
 * exact same workload and only the library underneath differs. */

#ifndef TPW_BENCH_COMMON_H
#define TPW_BENCH_COMMON_H

#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed formats: audio 48 kHz stereo S16, video 640x480 YUYV with the frame
 * rate left to the source. */
#define BENCH_AUDIO_RATE     48000
#define BENCH_AUDIO_CHANNELS 2
#define BENCH_VIDEO_WIDTH    640
#define BENCH_VIDEO_HEIGHT   480
#define BENCH_MAX_STREAMS    64

typedef struct {
    bool video;      /* capture video instead of audio */
    int n_streams;   /* capture streams to open */
    int duration;    /* seconds to run, 0 = until SIGINT/SIGTERM */
    bool separate;   /* bench_raw only: one context per stream */
} bench_options;

static volatile sig_atomic_t g_bench_running = 1;

static void bench_on_signal(int sig)
{
    (void)sig;
    g_bench_running = 0;
}

static void bench_usage(const char* prog, bool raw)
{
    fprintf(stderr,
            "usage: %s [-t audio|video] [-n streams] [-d seconds]%s\n"
            "\n"
            "  Opens N capture streams and discards what they deliver, so a\n"
            "  footprint tool can measure the process while it runs.\n"
            "\n"
            "  -t, --type audio|video  what to capture (default: audio)\n"
            "  -n, --streams <n>       capture streams to open (default: 1, max %d)\n"
            "  -d, --duration <s>      stop after N seconds (default: until Ctrl+C)\n"
            "%s"
            "  -h, --help              show this help\n",
            prog, raw ? " [--separate]" : "", BENCH_MAX_STREAMS,
            raw ? "      --separate          one thread loop + context per stream,\n"
                  "                          as tinypipewire does today\n"
                : "");
}

/* Returns 0 to run, 1 on a usage error, -1 when help was printed. */
static int bench_parse_options(int argc, char** argv, bool raw, bench_options* opts)
{
    static const struct option long_options[] = {
        { "type", required_argument, NULL, 't' },
        { "streams", required_argument, NULL, 'n' },
        { "duration", required_argument, NULL, 'd' },
        { "separate", no_argument, NULL, 1 },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    *opts = (bench_options){ .n_streams = 1 };

    int opt;
    while ((opt = getopt_long(argc, argv, "t:n:d:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 't':
            if (strcmp(optarg, "audio") == 0) {
                opts->video = false;
            } else if (strcmp(optarg, "video") == 0) {
                opts->video = true;
            } else {
                fprintf(stderr, "%s: --type must be audio or video\n", argv[0]);
                return 1;
            }
            break;
        case 'n':
            opts->n_streams = atoi(optarg);
            if (opts->n_streams < 1 || opts->n_streams > BENCH_MAX_STREAMS) {
                fprintf(stderr, "%s: --streams must be 1..%d\n", argv[0], BENCH_MAX_STREAMS);
                return 1;
            }
            break;
        case 'd':
            opts->duration = atoi(optarg);
            if (opts->duration <= 0) {
                fprintf(stderr, "%s: --duration must be a positive integer\n", argv[0]);
                return 1;
            }
            break;
        case 1:
            if (!raw) {
                bench_usage(argv[0], raw);
                return 1;
            }
            opts->separate = true;
            break;
        case 'h':
            bench_usage(argv[0], raw);
            return -1;
        default:
            bench_usage(argv[0], raw);
            return 1;
        }
    }

    signal(SIGINT, bench_on_signal);
    signal(SIGTERM, bench_on_signal);
    return 0;
}

#endif /* TPW_BENCH_COMMON_H */
