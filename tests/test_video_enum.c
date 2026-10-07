/* SPDX-License-Identifier: MIT */

/* Covers the video format queries without a camera: the rejections a caller
 * can hit, and the frame-rate ordering (reached via the internal header). */

#include <stddef.h>

#include "pwf/pwf_filter.h"
#include "pwf/pwf_stream.h"
#include "pwf_pw_core_internal.h"
#include "pwf_test.h"

static void ignore_data_cb(struct pwf_stream* stream, const struct pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void ignore_process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                               void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

static void test_framerate_order(void)
{
    struct pwf_video_format_info info = { 0 };

    /* Rates arrive in whatever order the device lists them and come back
     * highest first, which is what makes fps[0] the one to reach for. */
    pwf_video_insert_framerate(&info, 15);
    pwf_video_insert_framerate(&info, 30);
    pwf_video_insert_framerate(&info, 5);
    PWF_ASSERT_EQ(info.n_fps, (size_t)3);
    PWF_ASSERT_EQ(info.fps[0], 30);
    PWF_ASSERT_EQ(info.fps[1], 15);
    PWF_ASSERT_EQ(info.fps[2], 5);

    /* A repeat is dropped rather than stored twice: a range whose ends
     * meet reports the same rate as both min and max. */
    pwf_video_insert_framerate(&info, 15);
    PWF_ASSERT_EQ(info.n_fps, (size_t)3);

    /* Filling the array exactly keeps every rate. */
    struct pwf_video_format_info full = { 0 };
    const size_t cap = sizeof(full.fps) / sizeof(full.fps[0]);
    for (size_t i = 1; i <= cap; i++)
        pwf_video_insert_framerate(&full, (int)i);
    PWF_ASSERT_EQ(full.n_fps, cap);
    PWF_ASSERT_EQ(full.fps[0], (int)cap);
    PWF_ASSERT_EQ(full.fps[cap - 1], 1);

    /* Past that the slowest rate falls off and n_fps stays at the array's
     * size, so iterating to n_fps never reads beyond it. */
    pwf_video_insert_framerate(&full, 240);
    PWF_ASSERT_EQ(full.n_fps, cap);
    PWF_ASSERT_EQ(full.fps[0], 240);
    PWF_ASSERT_EQ(full.fps[1], (int)cap);
    PWF_ASSERT_EQ(full.fps[cap - 1], 2);

    /* A rate slower than everything stored is dropped, changing nothing. */
    pwf_video_insert_framerate(&full, 1);
    PWF_ASSERT_EQ(full.n_fps, cap);
    PWF_ASSERT_EQ(full.fps[0], 240);
    PWF_ASSERT_EQ(full.fps[cap - 1], 2);
}

static void test_stream_rejections(void)
{
    struct pwf_video_format_info fmts[4];
    size_t n = 99;

    /* No handle, no connection to ask over. */
    PWF_ASSERT_EQ(pwf_stream_get_target_video_formats(NULL, "some-node", fmts, 4, &n),
                  PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(n, (size_t)0);

    /* An audio stream is refused, not answered with an empty list, which
     * would instead read as "this camera offers nothing". */
    struct pwf_stream* audio = pwf_stream_create(PWF_DATA_AUDIO, ignore_data_cb, NULL);
    PWF_ASSERT(audio != NULL);
    PWF_ASSERT_EQ(pwf_stream_get_target_video_formats(audio, "some-node", fmts, 4, &n),
                  PWF_ERR_INVALID_ARG);
    pwf_stream_destroy(audio);

    struct pwf_stream* video = pwf_stream_create(PWF_DATA_VIDEO, ignore_data_cb, NULL);
    PWF_ASSERT(video != NULL);

    /* There is nowhere to report a count to. */
    PWF_ASSERT_EQ(pwf_stream_get_target_video_formats(video, "some-node", fmts, 4, NULL),
                  PWF_ERR_INVALID_ARG);

    /* NULL target with none set means there is nothing to ask about. */
    PWF_ASSERT_EQ(pwf_stream_get_target_video_formats(video, NULL, fmts, 4, &n),
                  PWF_ERR_INVALID_ARG);

    /* A name no node carries resolves to nothing. */
    PWF_ASSERT_EQ(
        pwf_stream_get_target_video_formats(video, "pwf-test-nonexistent-node", fmts, 4, &n),
        PWF_ERR_NOT_FOUND);

    /* NULL target falls back to the one already set, so an unresolvable
     * one still fails rather than picking some other device. */
    PWF_ASSERT_EQ(pwf_stream_set_target(video, "pwf-test-nonexistent-node"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_get_target_video_formats(video, NULL, fmts, 4, &n),
                  PWF_ERR_NOT_FOUND);

    pwf_stream_destroy(video);
}

static void test_filter_rejections(void)
{
    struct pwf_video_format_info fmts[4];
    size_t n = 99;

    PWF_ASSERT_EQ(pwf_filter_get_target_video_formats(NULL, "some-node", fmts, 4, &n),
                  PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(n, (size_t)0);

    struct pwf_filter* filter = pwf_filter_create("pwf-test-video-enum", ignore_process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    PWF_ASSERT_EQ(pwf_filter_get_target_video_formats(filter, "some-node", fmts, 4, NULL),
                  PWF_ERR_INVALID_ARG);

    /* The filter takes no NULL-target shorthand: it has no target of its
     * own until a port is linked, which happens after the format is fixed. */
    PWF_ASSERT_EQ(pwf_filter_get_target_video_formats(filter, NULL, fmts, 4, &n),
                  PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(
        pwf_filter_get_target_video_formats(filter, "pwf-test-nonexistent-node", fmts, 4, &n),
        PWF_ERR_NOT_FOUND);

    /* "node:port" is accepted; the node half is what has to resolve. */
    PWF_ASSERT_EQ(pwf_filter_get_target_video_formats(filter, "pwf-test-nonexistent-node:capture_0",
                                                       fmts, 4, &n),
                  PWF_ERR_NOT_FOUND);

    pwf_filter_destroy(filter);
}

int main(void)
{
    test_framerate_order();
    test_stream_rejections();
    test_filter_rejections();
    return 0;
}
