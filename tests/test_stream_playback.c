/* SPDX-License-Identifier: MIT */

/* The fill rules are pure buffer arithmetic, so they are exercised through
 * pwf_stream_playback_fill() directly — no output device, no graph, no
 * timing — the way the filter tests already drive pwf_filter_on_process().
 * A whitebox format setup avoids connecting, so these run on a machine with
 * no sink at all. */

#include <string.h>

#include "pwf_stream_internal.h"
#include "pwf_test.h"

#define RATE 48000
#define CHANNELS 2
#define FRAME (2 * CHANNELS) /* S16 stereo */

static size_t g_available;
static void* g_data;
static int64_t g_pts;
static int g_calls;
static size_t g_report; /* what the callback claims it wrote */

static void fill_cb(struct pwf_stream* stream, struct pwf_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    g_calls++;
    g_available = buf->available;
    g_data = buf->data;
    g_pts = buf->pts;
    if (g_report > 0)
        memset(buf->data, 0xAB, g_report > buf->available ? buf->available : g_report);
    buf->size = g_report;
}

/* A playback stream with a format but no connection, so no device is
 * needed. pwf_stream_set_audio_config() would connect; this does not. */
static struct pwf_stream* make_stream(void)
{
    struct pwf_stream* handle = pwf_stream_create_playback(fill_cb, NULL);
    PWF_ASSERT(handle != NULL);
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    stream->format.audio.sample_rate = RATE;
    stream->format.audio.channels = CHANNELS;
    stream->format.audio.format = SPA_AUDIO_FORMAT_S16;
    stream->bytes_per_frame = FRAME;
    return stream;
}

static void test_frame_size_helper(void)
{
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_U8, 2), (size_t)2);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_S16, 2), (size_t)4);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_S24, 1), (size_t)3);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_S32, 2), (size_t)8);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_F32, 6), (size_t)24);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_S24_32, 2), (size_t)8);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_S16, 0), (size_t)0);
    PWF_ASSERT_EQ(pwf_audio_bytes_per_frame(SPA_AUDIO_FORMAT_UNKNOWN, 2), (size_t)0);
}

/* The callback gets a writable region and the byte count it may use, and a
 * full fill is published whole. */
static void test_full_fill(void)
{
    struct pwf_stream* stream = make_stream();
    unsigned char region[FRAME * 16];

    g_calls = 0;
    g_report = sizeof(region);
    size_t written = pwf_stream_playback_fill(stream, region, sizeof(region), 1234);

    PWF_ASSERT_EQ(g_calls, 1);
    PWF_ASSERT(g_data == region);
    PWF_ASSERT_EQ(g_available, sizeof(region));
    PWF_ASSERT_EQ(g_pts, (int64_t)1234);
    PWF_ASSERT_EQ(written, sizeof(region));
    for (size_t i = 0; i < sizeof(region); i++)
        PWF_ASSERT_EQ(region[i], 0xAB);

    pwf_stream_destroy(stream);
}

/* A short fill keeps what the callback wrote and silences the rest. */
static void test_short_fill_is_silenced(void)
{
    struct pwf_stream* stream = make_stream();
    unsigned char region[FRAME * 16];
    memset(region, 0xFF, sizeof(region));

    g_report = FRAME * 4;
    size_t written = pwf_stream_playback_fill(stream, region, sizeof(region), -1);

    PWF_ASSERT_EQ(written, (size_t)(FRAME * 4));
    for (size_t i = 0; i < FRAME * 4; i++)
        PWF_ASSERT_EQ(region[i], 0xAB);
    for (size_t i = FRAME * 4; i < sizeof(region); i++)
        PWF_ASSERT_EQ(region[i], 0x00); /* not the 0xFF left behind */

    pwf_stream_destroy(stream);
}

/* Reporting nothing emits a silent cycle rather than stopping. */
static void test_zero_fill_is_all_silence(void)
{
    struct pwf_stream* stream = make_stream();
    unsigned char region[FRAME * 8];
    memset(region, 0xFF, sizeof(region));

    g_calls = 0;
    g_report = 0;
    size_t written = pwf_stream_playback_fill(stream, region, sizeof(region), -1);

    PWF_ASSERT_EQ(g_calls, 1);
    PWF_ASSERT_EQ(written, (size_t)0);
    for (size_t i = 0; i < sizeof(region); i++)
        PWF_ASSERT_EQ(region[i], 0x00);

    /* The stream is still usable: a second cycle runs normally. */
    g_report = FRAME;
    PWF_ASSERT_EQ(pwf_stream_playback_fill(stream, region, sizeof(region), -1), (size_t)FRAME);

    pwf_stream_destroy(stream);
}

/* An oversized report is clamped, and nothing past the region is read
 * or written — the guard bytes after it must survive untouched. */
static void test_oversized_report_is_clamped(void)
{
    struct pwf_stream* stream = make_stream();
    unsigned char backing[FRAME * 12];
    memset(backing, 0x5A, sizeof(backing));

    size_t available = FRAME * 8;
    g_report = available * 4;
    size_t written = pwf_stream_playback_fill(stream, backing, available, -1);

    PWF_ASSERT_EQ(written, available);
    for (size_t i = available; i < sizeof(backing); i++)
        PWF_ASSERT_EQ(backing[i], 0x5A);

    pwf_stream_destroy(stream);
}

/* A count that is not a whole number of frames is floored, and the partial
 * frame is silenced rather than emitted. */
static void test_partial_frame_is_truncated(void)
{
    struct pwf_stream* stream = make_stream();
    unsigned char region[FRAME * 8];
    memset(region, 0xFF, sizeof(region));

    g_report = FRAME * 3 + 1;
    size_t written = pwf_stream_playback_fill(stream, region, sizeof(region), -1);

    PWF_ASSERT_EQ(written, (size_t)(FRAME * 3));
    PWF_ASSERT_EQ(region[FRAME * 3], 0x00);

    pwf_stream_destroy(stream);
}

/* Direction is decided at creation: the existing constructor still makes a
 * capture stream, and a video format has nothing to connect to on playback. */
static void test_direction_and_video_rejection(void)
{
    struct pwf_stream* playback = pwf_stream_create_playback(fill_cb, NULL);
    PWF_ASSERT(playback != NULL);
    PWF_ASSERT_EQ(((struct pwf_stream*)playback)->direction, PWF_STREAM_DIRECTION_PLAYBACK);
    PWF_ASSERT_EQ(((struct pwf_stream*)playback)->type, PWF_DATA_AUDIO);

    struct pwf_video_config vcfg = { .width = 640, .height = 480, .pixel_format = "I420", .fps = 30 };
    PWF_ASSERT_EQ(pwf_stream_set_video_config(playback, &vcfg), PWF_ERR_INVALID_ARG);
    PWF_ASSERT(!((struct pwf_stream*)playback)->format_set);
    pwf_stream_destroy(playback);

    PWF_ASSERT(pwf_stream_create_playback(NULL, NULL) == NULL);
}

static void capture_cb(struct pwf_stream* stream, const struct pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void test_existing_constructor_is_still_capture(void)
{
    struct pwf_stream* audio = pwf_stream_create(PWF_DATA_AUDIO, capture_cb, NULL);
    PWF_ASSERT(audio != NULL);
    PWF_ASSERT_EQ(((struct pwf_stream*)audio)->direction, PWF_STREAM_DIRECTION_CAPTURE);
    pwf_stream_destroy(audio);

    struct pwf_stream* video = pwf_stream_create(PWF_DATA_VIDEO, capture_cb, NULL);
    PWF_ASSERT(video != NULL);
    PWF_ASSERT_EQ(((struct pwf_stream*)video)->direction, PWF_STREAM_DIRECTION_CAPTURE);
    pwf_stream_destroy(video);
}

/* Naming an output device is the same pre-connect step it is for capture,
 * and clearing it returns the stream to the default device. */
static void test_target_selection(void)
{
    struct pwf_stream* handle = pwf_stream_create_playback(fill_cb, NULL);
    PWF_ASSERT(handle != NULL);
    struct pwf_stream* stream = (struct pwf_stream*)handle;

    PWF_ASSERT(stream->target == NULL); /* default device until told otherwise */

    PWF_ASSERT_EQ(pwf_stream_set_target(handle, "alsa_output.some-sink"), PWF_OK);
    PWF_ASSERT(stream->target != NULL);
    PWF_ASSERT_EQ(strcmp(stream->target, "alsa_output.some-sink"), 0);

    PWF_ASSERT_EQ(pwf_stream_set_target(handle, NULL), PWF_OK);
    PWF_ASSERT(stream->target == NULL);

    pwf_stream_destroy(handle);
}

/* Starting before a format is configured is refused, the same way it is for
 * capture, so a playback stream cannot run without a negotiated cycle. */
static void test_start_requires_a_format(void)
{
    struct pwf_stream* handle = pwf_stream_create_playback(fill_cb, NULL);
    PWF_ASSERT(handle != NULL);

    PWF_ASSERT_EQ(pwf_stream_start(handle), PWF_ERR_NOT_CONFIGURED);
    PWF_ASSERT_EQ(pwf_stream_stop(handle, false), PWF_OK); /* stopping an idle stream is a no-op */

    pwf_stream_destroy(handle);
}

/* Repeated overruns collapse into one report per interval instead of one
 * per cycle, and the suppressed ones are counted rather than lost. */
static void test_overrun_log_is_rate_limited(void)
{
    struct pwf_stream* stream = make_stream();
    const uint64_t second = 1000000000ull;

    PWF_ASSERT(pwf_stream_playback_note_overrun(stream, second));      /* first always reports */
    PWF_ASSERT(!pwf_stream_playback_note_overrun(stream, second + 1));
    PWF_ASSERT(!pwf_stream_playback_note_overrun(stream, second + 2));
    PWF_ASSERT_EQ(stream->overrun_suppressed, (uint64_t)2);

    PWF_ASSERT(pwf_stream_playback_note_overrun(stream, second * 3)); /* interval elapsed */
    PWF_ASSERT_EQ(stream->overrun_suppressed, (uint64_t)2); /* the caller clears after logging */

    pwf_stream_destroy(stream);
}

int main(void)
{
    test_frame_size_helper();
    test_full_fill();
    test_short_fill_is_silenced();
    test_zero_fill_is_all_silence();
    test_oversized_report_is_clamped();
    test_partial_frame_is_truncated();
    test_direction_and_video_rejection();
    test_existing_constructor_is_still_capture();
    test_target_selection();
    test_start_requires_a_format();
    test_overrun_log_is_rate_limited();
    printf("test_stream_playback: all cases passed\n");
    return 0;
}
