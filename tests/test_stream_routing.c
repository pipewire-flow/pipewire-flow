/* SPDX-License-Identifier: MIT */

/* The routing rules are argument and state checking, and the pairing is
 * arithmetic, so both are exercised without a device. pwf_stream_pair_ports()
 * is reachable directly, which is what keeps the risky part testable — the
 * same separation that made the playback fill rules testable. */

#include <string.h>

#include "pwf_stream_internal.h"
#include "pwf_test.h"

static void on_data(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void on_fill(pwf_stream_h stream, pwf_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    buf->size = 0;
}

static pwf_stream_h make_capture(void)
{
    pwf_stream_h s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    return s;
}

/* Pairing: equal counts pair straight through, a short target is refused
 * outright, and a longer one reports what it left over. */
static void test_pairing(void)
{
    size_t surplus = 12345;

    PWF_ASSERT_EQ(pwf_stream_pair_ports(2, 2, &surplus), (size_t)2);
    PWF_ASSERT_EQ(surplus, (size_t)0);

    PWF_ASSERT_EQ(pwf_stream_pair_ports(1, 1, &surplus), (size_t)1);
    PWF_ASSERT_EQ(surplus, (size_t)0);

    /* stereo stream, mono device — refused whole, never half-wired */
    PWF_ASSERT_EQ(pwf_stream_pair_ports(2, 1, &surplus), (size_t)0);
    PWF_ASSERT_EQ(surplus, (size_t)0);

    /* mono stream, stereo device — links one, reports the other */
    PWF_ASSERT_EQ(pwf_stream_pair_ports(1, 2, &surplus), (size_t)1);
    PWF_ASSERT_EQ(surplus, (size_t)1);

    PWF_ASSERT_EQ(pwf_stream_pair_ports(2, 8, &surplus), (size_t)2);
    PWF_ASSERT_EQ(surplus, (size_t)6);

    /* a stream with no ports has nothing to pair */
    PWF_ASSERT_EQ(pwf_stream_pair_ports(0, 2, &surplus), (size_t)0);

    /* the surplus pointer is optional */
    PWF_ASSERT_EQ(pwf_stream_pair_ports(2, 2, NULL), (size_t)2);
}

/* A stream that declares nothing is wired by the session manager, as before. */
static void test_autoconnect_is_the_default(void)
{
    pwf_stream_h s = make_capture();
    PWF_ASSERT(((struct pwf_stream*)s)->autoconnect);
    pwf_stream_destroy(s);

    pwf_stream_h p = pwf_stream_create_playback(on_fill, NULL);
    PWF_ASSERT(p != NULL);
    PWF_ASSERT(((struct pwf_stream*)p)->autoconnect);
    pwf_stream_destroy(p);
}

/* The mode is fixed once the format has connected the stream. Refusing this
 * is not a formality: by then the node exists with its autoconnect property
 * already set, and a session manager may have wired it, so accepting the
 * change would tell the caller something untrue. */
static void test_mode_is_fixed_after_connect(void)
{
    pwf_stream_h s = make_capture();
    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };

    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, true), PWF_ERR_INVALID_ARG);

    pwf_stream_destroy(s);
}

/* Video connects through a different config call, so it gets the same rule
 * checked separately — in both directions, since the mode is fixed by the
 * stream being connected rather than by which way it was set. */
static void test_mode_is_fixed_after_video_connect(void)
{
    pwf_stream_h s = pwf_stream_create(PWF_DATA_VIDEO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    pwf_video_config cfg = { .width = 640, .height = 480, .pixel_format = "YUYV", .fps = 30 };

    PWF_ASSERT_EQ(pwf_stream_set_video_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, true), PWF_ERR_INVALID_ARG);
    PWF_ASSERT(((struct pwf_stream*)s)->autoconnect); /* unchanged by the refusals */

    pwf_stream_destroy(s);
}

/* The target hint and manual wiring are exclusive in BOTH call orders. */
static void test_hint_and_manual_are_exclusive(void)
{
    pwf_stream_h a = make_capture();
    PWF_ASSERT_EQ(pwf_stream_set_target(a, "some-device"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(a, false), PWF_ERR_INVALID_ARG);
    PWF_ASSERT(((struct pwf_stream*)a)->autoconnect); /* unchanged by the refusal */
    pwf_stream_destroy(a);

    pwf_stream_h b = make_capture();
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(b, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_target(b, "some-device"), PWF_ERR_INVALID_ARG);
    PWF_ASSERT(((struct pwf_stream*)b)->target == NULL); /* unchanged by the refusal */
    /* Clearing a target is not naming one, so it stays allowed. */
    PWF_ASSERT_EQ(pwf_stream_set_target(b, NULL), PWF_OK);
    pwf_stream_destroy(b);
}

/* Linking is refused before start and while the session manager still owns
 * the wiring. */
static void test_link_ordering_and_mode(void)
{
    pwf_stream_h auto_s = make_capture();
    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(auto_s, &cfg), PWF_OK);
    /* autoconnect is on: refused on the mode, before anything is looked up */
    PWF_ASSERT_EQ(pwf_stream_link(auto_s, "some-device"), PWF_ERR_INVALID_ARG);
    pwf_stream_destroy(auto_s);

    pwf_stream_h s = make_capture();
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    /* before the format, and before start */
    PWF_ASSERT_EQ(pwf_stream_link(s, "some-device"), PWF_ERR_NOT_CONFIGURED);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_link(s, "some-device"), PWF_ERR_NOT_CONFIGURED);

    /* argument checking does not depend on the graph either */
    PWF_ASSERT_EQ(pwf_stream_link(s, NULL), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_stream_link(s, ""), PWF_ERR_INVALID_ARG);

    pwf_stream_destroy(s);
}

/* Releasing what was never wired is a caller mistake, not a no-op. */
static void test_unlink_without_links_is_refused(void)
{
    pwf_stream_h s = make_capture();
    PWF_ASSERT_EQ(pwf_stream_unlink(s), PWF_ERR_NOT_CONFIGURED);

    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_unlink(s), PWF_ERR_NOT_CONFIGURED);

    pwf_stream_destroy(s);
}

/* A link waits for the stream's ports and then its target. Without a session manager the ports
 * never come and the wait times out, and with one the absent target is not found. */
static void test_link_reports_timeout_or_missing_target(void)
{
    pwf_stream_h s = make_capture();
    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);

    int res = pwf_stream_link(s, "pwf-test-no-such-node");
    PWF_ASSERT(res == PWF_ERR_TIMEOUT || res == PWF_ERR_NOT_FOUND);

    pwf_stream_destroy(s);
}

/* Opting out and never naming a device is a legitimate state: the stream runs
 * and simply carries nothing. */
static void test_opted_out_and_unlinked_runs(void)
{
    pwf_stream_h s = make_capture();
    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };

    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);
    PWF_ASSERT(((struct pwf_stream*)s)->links == NULL);
    PWF_ASSERT_EQ(pwf_stream_stop(s, false), PWF_OK);

    pwf_stream_destroy(s);
}

/* Works right after create(), before any format is set, and a NULL `out`
 * is a safe count-only query — no assumption about what devices exist. */
static void test_get_target_list(void)
{
    size_t count = 99;
    PWF_ASSERT_EQ(pwf_stream_get_target_list(NULL, NULL, 0, &count), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(count, (size_t)0);

    pwf_stream_h s = make_capture();
    PWF_ASSERT_EQ(pwf_stream_get_target_list(s, NULL, 0, NULL), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_stream_get_target_list(s, NULL, 0, &count), PWF_OK);

    pwf_target_info targets[8];
    size_t again = 0;
    PWF_ASSERT_EQ(pwf_stream_get_target_list(s, targets, 8, &again), PWF_OK);
    PWF_ASSERT_EQ(again, count);
    for (size_t i = 0; i < again && i < 8; i++)
        PWF_ASSERT(targets[i].name[0] != '\0');

    pwf_stream_destroy(s);
}

/* Releasing an unlinked stream is safe to call from teardown paths. */
static void test_release_is_safe_when_unlinked(void)
{
    pwf_stream_h s = make_capture();
    pwf_stream_release_links((struct pwf_stream*)s); /* must not crash */
    PWF_ASSERT(((struct pwf_stream*)s)->links == NULL);
    pwf_stream_destroy(s);

    pwf_stream_release_links(NULL); /* nor on nothing at all */
}

int main(void)
{
    test_pairing();
    test_autoconnect_is_the_default();
    test_mode_is_fixed_after_connect();
    test_mode_is_fixed_after_video_connect();
    test_hint_and_manual_are_exclusive();
    test_link_ordering_and_mode();
    test_unlink_without_links_is_refused();
    test_opted_out_and_unlinked_runs();
    test_link_reports_timeout_or_missing_target();
    test_release_is_safe_when_unlinked();
    test_get_target_list();
    printf("test_stream_routing: all cases passed\n");
    return 0;
}
