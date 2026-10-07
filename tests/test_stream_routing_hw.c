/* SPDX-License-Identifier: MIT */

/* Proves the library did the wiring, which is the property this feature
 * actually guarantees. A session manager may be running throughout: what
 * shows it stayed out is that the stream carries nothing until the
 * application asks, and reaches exactly the named device the moment it does.
 *
 * Runs against a source with a capture stream, a sink with a playback stream,
 * and a camera with a video stream: the call is meant to be direction- and
 * media-agnostic, and video reaches a different branch (its own config call,
 * and a pairing that is always one to one because video has no channels).
 * Exits 77 when no device at all is present; each case is skipped on its own
 * if its kind of device is missing. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pwf/pwf_stream.h>

#include "pwf_test.h"
#include "pwf_test_hw_discover.h"

#define TEST_SKIP 77

#define RATE 48000

/* Mono, so the test runs against whatever device it finds. A stereo stream
 * would be refused outright by any mono device — correctly, but it would make
 * the test depend on which device happens to be plugged in. Against a stereo
 * device this also exercises the surplus-channel path. */
#define CHANNELS 1

/* Long enough for the graph to settle after each wiring change. */
#define SETTLE_USEC (400 * 1000)

static unsigned g_buffers;
static int g_error;

static void on_data(struct pwf_stream* stream, const struct pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
    g_buffers++;
}

static void on_fill(struct pwf_stream* stream, struct pwf_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    g_buffers++;
    buf->size = 0; /* silence: this test is about wiring, not audio */
}

static void on_error(struct pwf_stream* stream, int code, void* user_data)
{
    (void)stream;
    (void)user_data;
    g_error = code;
}

/* Counts links against `node`, read from the graph rather than from the
 * library's own bookkeeping. `pw-link -l` lists only ports that have links,
 * and prints each link twice — once as a port heading, once as an arrow line
 * under the peer — so only the arrow lines are counted. */
static int links_to(const char* node)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pw-link -l 2>/dev/null | grep '%s' | grep -c '|' || true", node);

    FILE* p = popen(cmd, "r");
    if (!p)
        return -1;
    int n = -1;
    if (fscanf(p, "%d", &n) != 1)
        n = -1;
    pclose(p);
    return n;
}

/* Linking the instant the stream starts must work. Neither the node id nor
 * the ports exist yet at that moment, so this is the regression guard for
 * waiting on both rather than reading them once. */
static void link_immediately_after_start(const char* device)
{
    struct pwf_stream* s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);

    struct pwf_audio_config cfg = { .sample_rate = RATE, .channels = CHANNELS, .format = "S16" };
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);

    /* No settling delay on purpose. */
    PWF_ASSERT_EQ(pwf_stream_link(s, device), PWF_OK);

    pwf_stream_stop(s, false);
    pwf_stream_destroy(s);
}

/* One direction's worth of the whole lifecycle. */
static void exercise(struct pwf_stream* s, const char* device)
{
    struct pwf_audio_config cfg = { .sample_rate = RATE, .channels = CHANNELS, .format = "S16" };

    PWF_ASSERT_EQ(pwf_stream_set_error_callback(s, on_error), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);

    /* Nothing wired us. This is the assertion the feature exists for. */
    usleep(SETTLE_USEC);
    int before = links_to(device);
    g_buffers = 0;
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(g_buffers, 0u);

    PWF_ASSERT_EQ(pwf_stream_link(s, device), PWF_OK);
    usleep(SETTLE_USEC);
    int after = links_to(device);
    printf("  links %d -> %d (expected +%d)\n", before, after, CHANNELS);
    PWF_ASSERT(after == before + CHANNELS);

    /* Data flows once, and only once, we asked for it. */
    g_buffers = 0;
    usleep(SETTLE_USEC);
    PWF_ASSERT(g_buffers > 0);

    /* Stopping must NOT release the wiring — a stopped stream resumes on the
     * same device rather than silently running unconnected. */
    PWF_ASSERT_EQ(pwf_stream_stop(s, false), PWF_OK);
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(links_to(device), after);

    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);
    g_buffers = 0;
    usleep(SETTLE_USEC);
    PWF_ASSERT(g_buffers > 0);
    PWF_ASSERT_EQ(links_to(device), after);

    /* Linking again while linked is refused, and leaves the links alone. */
    PWF_ASSERT_EQ(pwf_stream_link(s, device), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(links_to(device), after);

    /* Releasing puts the graph back where it started. */
    PWF_ASSERT_EQ(pwf_stream_unlink(s), PWF_OK);
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(links_to(device), before);
    PWF_ASSERT_EQ(pwf_stream_unlink(s), PWF_ERR_NOT_CONFIGURED);

    /* And it can be wired again afterwards. */
    PWF_ASSERT_EQ(pwf_stream_link(s, device), PWF_OK);
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(links_to(device), after);

    PWF_ASSERT_EQ(g_error, 0);

    pwf_stream_stop(s, false);
    pwf_stream_destroy(s);

    /* Destroy releases what stop kept. */
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(links_to(device), before);
}

/* Video takes a different route through the library — its own config call, its
 * own branch for how many ports to expect, and a pairing that is always one to
 * one because video has no channels. None of that is reachable from the audio
 * cases above. */
static void exercise_video(const char* camera)
{
    struct pwf_stream* s = pwf_stream_create(PWF_DATA_VIDEO, on_data, NULL);
    PWF_ASSERT(s != NULL);

    struct pwf_video_config cfg = { .width = 640, .height = 480, .pixel_format = "YUYV", .fps = 30 };
    PWF_ASSERT_EQ(pwf_stream_set_error_callback(s, on_error), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_video_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);

    usleep(SETTLE_USEC);
    int before = links_to(camera);
    g_buffers = 0;
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(g_buffers, 0u); /* nothing wired us */

    PWF_ASSERT_EQ(pwf_stream_link(s, camera), PWF_OK);
    usleep(SETTLE_USEC);
    int after = links_to(camera);
    printf("  links %d -> %d (expected +1: video has no channels)\n", before, after);
    PWF_ASSERT(after == before + 1);

    /* Frames only after we asked. */
    g_buffers = 0;
    usleep(SETTLE_USEC * 3);
    printf("  frames while linked: %u\n", g_buffers);
    PWF_ASSERT(g_buffers > 0);

    /* Same lifetime rules as audio. */
    PWF_ASSERT_EQ(pwf_stream_stop(s, false), PWF_OK);
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(links_to(camera), after);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);
    g_buffers = 0;
    usleep(SETTLE_USEC * 3);
    PWF_ASSERT(g_buffers > 0);

    PWF_ASSERT_EQ(pwf_stream_unlink(s), PWF_OK);
    usleep(SETTLE_USEC);
    PWF_ASSERT_EQ(links_to(camera), before);

    PWF_ASSERT_EQ(g_error, 0);

    pwf_stream_stop(s, false);
    pwf_stream_destroy(s);
}

int main(void)
{
    char source[256], sink[256], camera[256];
    bool have_source = pwf_test_find_node("Audio/Source", source, sizeof(source));
    bool have_sink = pwf_test_find_node("Audio/Sink", sink, sizeof(sink));
    bool have_camera = pwf_test_find_node("Video/Source", camera, sizeof(camera));

    if (!have_source && !have_sink && !have_camera) {
        printf("no audio device present, skipping\n");
        return TEST_SKIP;
    }

    if (have_source) {
        printf("capture -> %s\n", source);
        struct pwf_stream* s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
        if (!s) {
            printf("no PipeWire connection, skipping\n");
            return TEST_SKIP;
        }

        /* The node pwf_test_find_node() found must be in the same stream's
         * own target list — they both read Audio/Source from the graph. */
        struct pwf_target_info targets[64];
        size_t n = 0;
        PWF_ASSERT_EQ(pwf_stream_get_target_list(s, targets, 64, &n), PWF_OK);
        bool listed = false;
        for (size_t i = 0; i < n && i < 64; i++)
            listed = listed || strcmp(targets[i].name, source) == 0;
        PWF_ASSERT(listed);

        g_error = 0;
        exercise(s, source);
        link_immediately_after_start(source);
    }

    if (have_sink) {
        printf("playback -> %s\n", sink);
        struct pwf_stream* s = pwf_stream_create_playback(on_fill, NULL);
        if (!s) {
            printf("no PipeWire connection, skipping\n");
            return TEST_SKIP;
        }
        g_error = 0;
        exercise(s, sink);
    }

    if (have_camera) {
        printf("video capture -> %s\n", camera);
        g_error = 0;
        exercise_video(camera);
    }

    printf("test_stream_routing_hw: passed\n");
    return 0;
}
