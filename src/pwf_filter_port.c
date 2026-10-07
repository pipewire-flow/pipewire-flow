/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/keys.h>

#include "pwf_filter_internal.h"
#include "pwf_log_internal.h"
#include "pwf_spa_format_internal.h"

static void* pwf_filter_add_port_common(struct pwf_filter* filter, enum pwf_filter_port_direction direction,
                                         const struct spa_pod** params, uint32_t n_params,
                                         enum pw_filter_port_flags flags)
{
    enum spa_direction pw_dir = (direction == PWF_FILTER_PORT_INPUT) ? SPA_DIRECTION_INPUT : SPA_DIRECTION_OUTPUT;

    /* Name the port deterministically so pwf_filter_port_link() can find
     * this port's own global in the registry later. */
    char name[sizeof(((struct pwf_filter_port*)NULL)->pw_port_name)];
    snprintf(name, sizeof(name), "pwf_port_%zu", filter->n_ports);

    struct pw_properties* props = pw_properties_new(PW_KEY_PORT_NAME, name, NULL);
    if (!props)
        return NULL;

    pw_thread_loop_lock(filter->conn.loop);
    void* port_data =
        pw_filter_add_port(filter->pw_filter, pw_dir, flags,
                            sizeof(struct pwf_filter_port), props, params, n_params);
    pw_thread_loop_unlock(filter->conn.loop);

    if (port_data)
        memcpy(((struct pwf_filter_port*)port_data)->pw_port_name, name, sizeof(name));
    return port_data;
}

enum pwf_data_type pwf_filter_port_get_type(struct pwf_filter_port* port_handle)
{
    struct pwf_filter_port* port = (struct pwf_filter_port*)port_handle;
    return port ? port->media_type : PWF_DATA_AUDIO;
}

struct pwf_filter_port* pwf_filter_add_audio_port(struct pwf_filter* handle, enum pwf_filter_port_direction direction,
                                                   const struct pwf_audio_config* config)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter || !config || filter->state != PWF_FILTER_STATE_CREATED)
        return NULL;
    if (config->sample_rate <= 0 || config->channels <= 0)
        return NULL;

    enum spa_audio_format fmt = pwf_spa_lookup_audio_format(config->format ? config->format : "S16");
    if (fmt == SPA_AUDIO_FORMAT_UNKNOWN)
        return NULL;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[3];
    params[0] = pwf_spa_build_audio_format(&b, config, fmt);
    params[1] = pwf_spa_build_meta_header(&b);
    params[2] = pwf_spa_build_cpu_buffers(&b);

    void* port_data = pwf_filter_add_port_common(filter, direction, params, 3, PW_FILTER_PORT_FLAG_MAP_BUFFERS);
    if (!port_data)
        return NULL;

    struct pwf_filter_port* port = port_data;
    port->filter = filter;
    port->direction = direction;
    port->media_type = PWF_DATA_AUDIO;
    port->config.audio.sample_rate = config->sample_rate;
    port->config.audio.channels = config->channels;
    port->config.audio.format = fmt;

    if (!pwf_filter_add_port_to_list(filter, port))
        return NULL;

    return port;
}

int pwf_filter_push_port_data(struct pwf_filter* handle, struct pwf_filter_port* port_handle, const void* data,
                              size_t size, int64_t pts)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    struct pwf_filter_port* port = (struct pwf_filter_port*)port_handle;
    if (!filter || !port || port->filter != filter || port->direction != PWF_FILTER_PORT_INPUT)
        return PWF_ERR_INVALID_ARG;
    if (port->media_type == PWF_DATA_EVENT)
        return PWF_ERR_INVALID_ARG;
    if (size > 0 && !data)
        return PWF_ERR_INVALID_ARG;

    /* The cycle never holds this lock across the callback, so pushing from
     * inside the callback takes it just as safely as any other thread. */
    pthread_mutex_lock(&filter->push_lock);

    if (size > port->pushed_capacity) {
        void* grown = realloc(port->pushed_data, size);
        if (!grown) {
            pthread_mutex_unlock(&filter->push_lock);
            pwf_log_error("filter '%s': failed to grow push buffer to %zu bytes",
                          filter->name ? filter->name : "pwf-filter", size);
            return PWF_ERR_NO_MEMORY;
        }
        port->pushed_data = grown;
        port->pushed_capacity = size;
    }
    if (size > 0)
        memcpy(port->pushed_data, data, size);
    port->pushed_size = size;
    port->pushed_pts = pts;
    port->pushed_pending = true;

    pthread_mutex_unlock(&filter->push_lock);
    return PWF_OK;
}

struct pwf_filter_port* pwf_filter_add_signal_port(struct pwf_filter* handle, enum pwf_filter_port_direction direction)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter || filter->state != PWF_FILTER_STATE_CREATED)
        return NULL;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[2];
    params[0] = pwf_spa_build_signal_format(&b);
    params[1] = pwf_spa_build_cpu_buffers(&b);

    void* port_data = pwf_filter_add_port_common(filter, direction, params, 2, PW_FILTER_PORT_FLAG_MAP_BUFFERS);
    if (!port_data)
        return NULL;

    struct pwf_filter_port* port = port_data;
    port->filter = filter;
    port->direction = direction;
    port->media_type = PWF_DATA_SIGNAL;

    if (!pwf_filter_add_port_to_list(filter, port))
        return NULL;

    return port;
}

struct pwf_filter_port* pwf_filter_add_event_port(struct pwf_filter* handle, enum pwf_filter_port_direction direction)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter || filter->state != PWF_FILTER_STATE_CREATED)
        return NULL;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[2];
    params[0] = pwf_spa_build_event_format(&b);
    params[1] = pwf_spa_build_cpu_buffers(&b);

    void* port_data = pwf_filter_add_port_common(filter, direction, params, 2, PW_FILTER_PORT_FLAG_MAP_BUFFERS);
    if (!port_data)
        return NULL;

    struct pwf_filter_port* port = port_data;
    port->filter = filter;
    port->direction = direction;
    port->media_type = PWF_DATA_EVENT;

    if (!pwf_filter_add_port_to_list(filter, port))
        return NULL;

    return port;
}

struct pwf_filter_port* pwf_filter_add_video_port_ex(struct pwf_filter* handle,
                                                     enum pwf_filter_port_direction direction,
                                                     const struct pwf_video_config* config,
                                                     const struct pwf_filter_port_opts* opts)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter || !config || !config->pixel_format || filter->state != PWF_FILTER_STATE_CREATED)
        return NULL;
    if (config->width <= 0 || config->height <= 0 || config->fps < 0)
        return NULL;

    bool want_dmabuf = opts && opts->memory == PWF_PORT_MEMORY_DMABUF;
    /* DMABUF is an import-only path for video input; refuse it on output. */
    if (want_dmabuf && direction != PWF_FILTER_PORT_INPUT)
        return NULL;

    bool is_mjpg = pwf_spa_pixel_format_is_mjpg(config->pixel_format);
    bool is_h264 = pwf_spa_pixel_format_is_h264(config->pixel_format);
    bool is_encoded = is_mjpg || is_h264;
    /* Encoded frames (MJPEG, H.264) are never handed out as DMABUF. */
    if (is_encoded && want_dmabuf)
        return NULL;

    enum spa_video_format fmt = SPA_VIDEO_FORMAT_ENCODED;
    if (!is_encoded) {
        fmt = pwf_spa_lookup_pixel_format(config->pixel_format);
        if (fmt == SPA_VIDEO_FORMAT_UNKNOWN)
            return NULL;
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
    if (!want_dmabuf)
        params[n_params++] = pwf_spa_build_cpu_buffers(&b);

    /* DMABUF buffers are not CPU-mapped, so omit MAP_BUFFERS. */
    enum pw_filter_port_flags flags = want_dmabuf ? 0 : PW_FILTER_PORT_FLAG_MAP_BUFFERS;
    void* port_data = pwf_filter_add_port_common(filter, direction, params, n_params, flags);
    if (!port_data)
        return NULL;

    struct pwf_filter_port* port = port_data;
    port->filter = filter;
    port->direction = direction;
    port->media_type = PWF_DATA_VIDEO;
    port->config.video.width = config->width;
    port->config.video.height = config->height;
    port->config.video.format = fmt;
    port->use_dmabuf = want_dmabuf;

    if (!pwf_filter_add_port_to_list(filter, port))
        return NULL;

    return port;
}

struct pwf_filter_port* pwf_filter_add_video_port(struct pwf_filter* handle, enum pwf_filter_port_direction direction,
                                                   const struct pwf_video_config* config)
{
    return pwf_filter_add_video_port_ex(handle, direction, config, NULL);
}

int pwf_filter_port_set_hold(struct pwf_filter_port* port_handle, bool enable)
{
    struct pwf_filter_port* port = (struct pwf_filter_port*)port_handle;
    if (!port || port->direction != PWF_FILTER_PORT_INPUT)
        return PWF_ERR_INVALID_ARG;
    if (port->filter->state != PWF_FILTER_STATE_CREATED)
        return PWF_ERR_INVALID_ARG;

    port->hold_enabled = enable;
    return PWF_OK;
}
