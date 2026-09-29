/* SPDX-License-Identifier: MIT */

/* The raw pw_stream side of the footprint comparison: the same capture work
 * as bench_tpw.c with the same stream properties, flags and formats, written
 * the way a careful libpipewire user would — one thread loop and one context
 * for every stream. --separate gives each stream its own loop and context
 * instead, which is what tinypipewire does today. */

#include <stdatomic.h>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>

#include "bench_common.h"

struct bench_conn {
    struct pw_thread_loop* loop;
    struct pw_context* context;
    struct pw_core* core;
};

struct bench_stream {
    struct pw_stream* stream;
    struct spa_hook listener;
    struct bench_conn* conn;
    atomic_ulong buffers;
};

static struct bench_conn g_conns[BENCH_MAX_STREAMS];
static struct bench_stream g_streams[BENCH_MAX_STREAMS];

static void on_process(void* data)
{
    struct bench_stream* s = data;
    struct pw_buffer* b = pw_stream_dequeue_buffer(s->stream);
    if (!b)
        return;
    atomic_fetch_add_explicit(&s->buffers, 1, memory_order_relaxed);
    pw_stream_queue_buffer(s->stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static int conn_open(struct bench_conn* conn)
{
    conn->loop = pw_thread_loop_new("bench-raw-loop", NULL);
    if (!conn->loop || pw_thread_loop_start(conn->loop) < 0)
        return -1;

    pw_thread_loop_lock(conn->loop);
    conn->context = pw_context_new(pw_thread_loop_get_loop(conn->loop), NULL, 0);
    if (conn->context)
        conn->core = pw_context_connect(conn->context, NULL, 0);
    pw_thread_loop_unlock(conn->loop);

    return conn->core ? 0 : -1;
}

static void conn_close(struct bench_conn* conn)
{
    if (!conn->loop)
        return;
    pw_thread_loop_stop(conn->loop);
    if (conn->core)
        pw_core_disconnect(conn->core);
    if (conn->context)
        pw_context_destroy(conn->context);
    pw_thread_loop_destroy(conn->loop);
}

/* Mirrors tpw_spa_build_audio_format()/tpw_spa_build_video_format() for the
 * formats bench_tpw requests. */
static const struct spa_pod* build_format(struct spa_pod_builder* b, bool video)
{
    if (!video) {
        struct spa_audio_info_raw info = {
            .format = SPA_AUDIO_FORMAT_S16,
            .rate = BENCH_AUDIO_RATE,
            .channels = BENCH_AUDIO_CHANNELS,
        };
        return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
    }

    struct spa_rectangle size = SPA_RECTANGLE(BENCH_VIDEO_WIDTH, BENCH_VIDEO_HEIGHT);
    struct spa_fraction fr_def = SPA_FRACTION(30, 1);
    struct spa_fraction fr_min = SPA_FRACTION(0, 1);
    struct spa_fraction fr_max = SPA_FRACTION(1000, 1);
    return spa_pod_builder_add_object(b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format,   SPA_POD_Id(SPA_VIDEO_FORMAT_YUY2),
        SPA_FORMAT_VIDEO_size,     SPA_POD_Rectangle(&size),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&fr_def, &fr_min, &fr_max));
}

static int stream_open(struct bench_stream* s, struct bench_conn* conn, bool video)
{
    s->conn = conn;
    atomic_init(&s->buffers, 0UL);

    struct pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, video ? "Video" : "Audio",
                                                     PW_KEY_MEDIA_CATEGORY, "Capture", NULL);

    uint8_t pod_buf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(pod_buf, sizeof(pod_buf));
    const struct spa_pod* params[1] = { build_format(&b, video) };

    pw_thread_loop_lock(conn->loop);
    s->stream = pw_stream_new(conn->core, "bench-raw-stream", props);
    int res = -1;
    if (s->stream) {
        pw_stream_add_listener(s->stream, &s->listener, &stream_events, s);
        res = pw_stream_connect(s->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                    PW_STREAM_FLAG_RT_PROCESS,
                                params, 1);
    }
    pw_thread_loop_unlock(conn->loop);
    return res;
}

static void stream_close(struct bench_stream* s)
{
    if (!s->stream)
        return;
    pw_thread_loop_lock(s->conn->loop);
    pw_stream_destroy(s->stream);
    pw_thread_loop_unlock(s->conn->loop);
}

int main(int argc, char** argv)
{
    bench_options opts;
    int res = bench_parse_options(argc, argv, true, &opts);
    if (res != 0)
        return res < 0 ? 0 : res;

    pw_init(&argc, &argv);

    int n_conns = opts.separate ? opts.n_streams : 1;
    int ret = 1;

    for (int i = 0; i < n_conns; i++) {
        if (conn_open(&g_conns[i]) < 0) {
            fprintf(stderr, "bench_raw: failed to connect to PipeWire\n");
            goto out;
        }
    }

    for (int i = 0; i < opts.n_streams; i++) {
        if (stream_open(&g_streams[i], &g_conns[opts.separate ? i : 0], opts.video) < 0) {
            fprintf(stderr, "bench_raw: failed to start stream %d\n", i);
            goto out;
        }
    }

    for (int elapsed = 0; g_bench_running && (opts.duration == 0 || elapsed < opts.duration); elapsed++)
        sleep(1);

    ret = 0;
    for (int i = 0; i < opts.n_streams; i++)
        printf("bench_raw: stream %d received %lu buffers\n", i, atomic_load(&g_streams[i].buffers));

out:
    for (int i = 0; i < opts.n_streams; i++)
        stream_close(&g_streams[i]);
    for (int i = 0; i < n_conns; i++)
        conn_close(&g_conns[i]);
    pw_deinit();
    return ret;
}
