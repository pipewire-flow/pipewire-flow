/* SPDX-License-Identifier: MIT */

#include <spa/buffer/buffer.h>

#include "pwf/pwf_stream.h"
#include "pwf_stream_internal.h" /* whitebox: feed a synthetic DMABUF buffer, inspect use_dmabuf */
#include "pwf_test.h"

static void noop_data_cb(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void noop_playback_cb(pwf_stream_h stream, pwf_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

/* Feeds a synthetic buffer straight to the accessor and checks it extracts
 * the DMABUF plane's fd/offset/stride/size, skipping a non-DMABUF block. */
static void test_plane_extraction(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;

    struct spa_chunk chunk0 = { .offset = 0, .size = 100, .stride = 640 };
    struct spa_data datas[2] = {
        { .type = SPA_DATA_DmaBuf, .fd = 42, .mapoffset = 16, .maxsize = 200, .chunk = &chunk0 },
        { .type = SPA_DATA_MemPtr, .fd = -1 },
    };
    struct spa_buffer sb = { .n_datas = 2, .datas = datas };
    stream->current_dmabuf_buf = &sb;

    pwf_dmabuf_plane planes[4];
    PWF_ASSERT_EQ(pwf_stream_get_dmabuf_planes(handle, planes, 4), (size_t)1);
    PWF_ASSERT_EQ(planes[0].fd, 42);
    PWF_ASSERT_EQ(planes[0].offset, 16u);
    PWF_ASSERT_EQ(planes[0].stride, 640u);
    PWF_ASSERT_EQ(planes[0].size, 100u);
    PWF_ASSERT(stream->dmabuf_retrieved);

    stream->current_dmabuf_buf = NULL;
}

/* NV12: two planes as two distinct file descriptors. */
static void test_nv12_two_fds(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;

    struct spa_chunk chunk_y = { .size = 640 * 480, .stride = 640 };
    struct spa_chunk chunk_uv = { .size = 640 * 480 / 2, .stride = 640 };
    struct spa_data datas[2] = {
        { .type = SPA_DATA_DmaBuf, .fd = 10, .mapoffset = 0, .maxsize = 640 * 480, .chunk = &chunk_y },
        { .type = SPA_DATA_DmaBuf, .fd = 11, .mapoffset = 0, .maxsize = 640 * 480 / 2, .chunk = &chunk_uv },
    };
    struct spa_buffer sb = { .n_datas = 2, .datas = datas };
    stream->current_dmabuf_buf = &sb;

    pwf_dmabuf_plane planes[4];
    PWF_ASSERT_EQ(pwf_stream_get_dmabuf_planes(handle, planes, 4), (size_t)2);
    PWF_ASSERT_EQ(planes[0].fd, 10);
    PWF_ASSERT_EQ(planes[0].stride, 640u);
    PWF_ASSERT_EQ(planes[1].fd, 11);
    PWF_ASSERT_EQ(planes[1].stride, 640u);

    stream->current_dmabuf_buf = NULL;
}

/* I420: three planes sharing one file descriptor at different offsets. */
static void test_i420_shared_fd(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;

    struct spa_chunk chunk_y = { .size = 640 * 480, .stride = 640 };
    struct spa_chunk chunk_u = { .size = 640 * 480 / 4, .stride = 320 };
    struct spa_chunk chunk_v = { .size = 640 * 480 / 4, .stride = 320 };
    struct spa_data datas[3] = {
        { .type = SPA_DATA_DmaBuf, .fd = 20, .mapoffset = 0, .maxsize = 640 * 480, .chunk = &chunk_y },
        { .type = SPA_DATA_DmaBuf,
          .fd = 20,
          .mapoffset = 640 * 480,
          .maxsize = 640 * 480 / 4,
          .chunk = &chunk_u },
        { .type = SPA_DATA_DmaBuf,
          .fd = 20,
          .mapoffset = 640 * 480 + 640 * 480 / 4,
          .maxsize = 640 * 480 / 4,
          .chunk = &chunk_v },
    };
    struct spa_buffer sb = { .n_datas = 3, .datas = datas };
    stream->current_dmabuf_buf = &sb;

    pwf_dmabuf_plane planes[4];
    PWF_ASSERT_EQ(pwf_stream_get_dmabuf_planes(handle, planes, 4), (size_t)3);
    PWF_ASSERT_EQ(planes[0].fd, 20);
    PWF_ASSERT_EQ(planes[0].offset, 0u);
    PWF_ASSERT_EQ(planes[1].fd, 20);
    PWF_ASSERT_EQ(planes[1].offset, (uint32_t)(640 * 480));
    PWF_ASSERT_EQ(planes[1].stride, 320u);
    PWF_ASSERT_EQ(planes[2].fd, 20);
    PWF_ASSERT_EQ(planes[2].offset, (uint32_t)(640 * 480 + 640 * 480 / 4));
    PWF_ASSERT_EQ(planes[2].stride, 320u);

    stream->current_dmabuf_buf = NULL;
}

int main(void)
{
    pwf_stream_h stream = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
    PWF_ASSERT(stream != NULL);

    pwf_video_config cfg = { .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 };

    /* opts == NULL is exactly the non-_ex call: no DMABUF is requested. */
    PWF_ASSERT_EQ(pwf_stream_set_video_config_ex(stream, &cfg, NULL), PWF_OK);
    PWF_ASSERT(!((struct pwf_stream*)stream)->use_dmabuf);

    /* A plain caller that has never heard of the _ex call or DMABUF stays
     * exactly as it was before this feature existed. */
    PWF_ASSERT_EQ(pwf_stream_set_video_config(stream, &cfg), PWF_OK);
    PWF_ASSERT(!((struct pwf_stream*)stream)->use_dmabuf);

    /* The accessor never fabricates a plane on a non-DMABUF stream, or for
     * a NULL handle. */
    pwf_dmabuf_plane planes[4];
    PWF_ASSERT_EQ(pwf_stream_get_dmabuf_planes(stream, planes, 4), 0);
    PWF_ASSERT_EQ(pwf_stream_get_dmabuf_planes(NULL, planes, 4), 0);

    /* DMABUF is accepted on a video capture stream. */
    pwf_stream_dmabuf_opts dmabuf_opts = { .memory = PWF_PORT_MEMORY_DMABUF };
    PWF_ASSERT_EQ(pwf_stream_set_video_config_ex(stream, &cfg, &dmabuf_opts), PWF_OK);
    PWF_ASSERT(((struct pwf_stream*)stream)->use_dmabuf);

    /* Outside a cycle (no current buffer) the accessor still returns 0. */
    PWF_ASSERT_EQ(pwf_stream_get_dmabuf_planes(stream, planes, 4), 0);

    /* With a real DMABUF frame present, the accessor extracts its plane. */
    test_plane_extraction(stream);

    /* Multi-plane pixel formats expose every plane, in both layouts a
     * source may use: distinct file descriptors, or one descriptor at
     * different offsets. */
    test_nv12_two_fds(stream);
    test_i420_shared_fd(stream);

    pwf_stream_destroy(stream);

    /* DMABUF is video-capture-only: rejected on an audio stream and on a
     * playback stream, the same guard pwf_stream_set_video_config() already
     * applies. */
    pwf_stream_h audio = pwf_stream_create(PWF_DATA_AUDIO, noop_data_cb, NULL);
    PWF_ASSERT(audio != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_video_config_ex(audio, &cfg, &dmabuf_opts), PWF_ERR_INVALID_ARG);
    pwf_stream_destroy(audio);

    pwf_stream_h playback = pwf_stream_create_playback(noop_playback_cb, NULL);
    PWF_ASSERT(playback != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_video_config_ex(playback, &cfg, &dmabuf_opts), PWF_ERR_INVALID_ARG);
    pwf_stream_destroy(playback);

    /* MJPEG and H.264 frames are never handed out as DMABUF. */
    pwf_stream_h mjpg = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
    PWF_ASSERT(mjpg != NULL);
    pwf_video_config mjpg_cfg = { .width = 640, .height = 480, .pixel_format = "MJPG", .fps = 30 };
    PWF_ASSERT_EQ(pwf_stream_set_video_config_ex(mjpg, &mjpg_cfg, &dmabuf_opts), PWF_ERR_INVALID_ARG);
    pwf_stream_destroy(mjpg);

    pwf_stream_h h264 = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
    PWF_ASSERT(h264 != NULL);
    pwf_video_config h264_cfg = { .width = 640, .height = 480, .pixel_format = "H264", .fps = 30 };
    PWF_ASSERT_EQ(pwf_stream_set_video_config_ex(h264, &h264_cfg, &dmabuf_opts), PWF_ERR_INVALID_ARG);
    pwf_stream_destroy(h264);

    return 0;
}
