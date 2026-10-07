/* SPDX-License-Identifier: MIT */

#include "pwf_spa_format_internal.h"
#include "pwf_stream_internal.h"

int pwf_stream_set_video_config_ex(struct pwf_stream* handle, const struct pwf_video_config* config,
                                    const struct pwf_stream_dmabuf_opts* opts)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream || stream->type != PWF_DATA_VIDEO || !config || !config->pixel_format)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, true, __func__))
        return PWF_ERR_IN_CALLBACK;
    /* Playback is audio-only, so a video format has nothing to connect to. */
    if (stream->direction != PWF_STREAM_DIRECTION_CAPTURE)
        return PWF_ERR_INVALID_ARG;
    if (config->width <= 0 || config->height <= 0 || config->fps < 0)
        return PWF_ERR_INVALID_FORMAT;

    bool use_dmabuf = opts && opts->memory == PWF_PORT_MEMORY_DMABUF;
    bool is_mjpg = pwf_spa_pixel_format_is_mjpg(config->pixel_format);
    bool is_h264 = pwf_spa_pixel_format_is_h264(config->pixel_format);
    bool is_encoded = is_mjpg || is_h264;
    /* Encoded frames (MJPEG, H.264) are never handed out as DMABUF; nothing
     * here would ever negotiate it, so refuse the request. */
    if (is_encoded && use_dmabuf)
        return PWF_ERR_INVALID_ARG;

    enum spa_video_format fmt = SPA_VIDEO_FORMAT_ENCODED;
    if (!is_encoded) {
        fmt = pwf_spa_lookup_pixel_format(config->pixel_format);
        if (fmt == SPA_VIDEO_FORMAT_UNKNOWN)
            return PWF_ERR_INVALID_FORMAT;
    }

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[3];
    if (is_mjpg)
        params[0] = pwf_spa_build_video_format_mjpg(&b, config);
    else if (is_h264)
        params[0] = pwf_spa_build_video_format_h264(&b, config);
    else
        params[0] = pwf_spa_build_video_format(&b, config, fmt);
    params[1] = pwf_spa_build_meta_header(&b);
    /* DMABUF's dataType is applied later in param_changed (add-time crashes
     * 1.0.5); restricting it here too would fight that deferred param. */
    uint32_t n_params = 2;
    if (!use_dmabuf)
        params[n_params++] = pwf_spa_build_cpu_buffers(&b);

    /* pwf_stream_internal_connect() owns stream->use_dmabuf: it must set
     * it only once the new pw_stream exists, not before. */
    int res = pwf_stream_internal_connect(stream, params, n_params, use_dmabuf);
    if (res < 0)
        return res;

    stream->format.video.width = config->width;
    stream->format.video.height = config->height;
    stream->format.video.format = fmt;
    stream->format_set = true;
    stream->state = PWF_STREAM_STATE_FORMAT_SET;
    return PWF_OK;
}

int pwf_stream_set_video_config(struct pwf_stream* handle, const struct pwf_video_config* config)
{
    return pwf_stream_set_video_config_ex(handle, config, NULL);
}
