/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/keys.h>
#include <spa/utils/dict.h>

#include "pwf_filter_internal.h"
#include "pwf_log_internal.h"
#include "pwf_spa_format_internal.h"

/* How long pwf_filter_stop(..., true) waits for a flush to actually drain
 * before giving up and stopping anyway. */
#define PWF_FILTER_DRAIN_TIMEOUT_NSEC (5 * SPA_NSEC_PER_SEC)

/* Reports a port that negotiated a format other than the one it asked for,
 * which happens when another consumer already configured the source. The
 * frame rate is not compared: fps == 0 asks the source to choose one. */
static void pwf_filter_check_negotiated_video(struct pwf_filter* filter, struct pwf_filter_port* port,
                                               const struct spa_pod* param)
{
    uint32_t media_type, media_subtype;
    if (port->media_type != PWF_DATA_VIDEO)
        return;
    if (spa_format_parse(param, &media_type, &media_subtype) < 0 || media_type != SPA_MEDIA_TYPE_video)
        return;

    enum spa_video_format got = SPA_VIDEO_FORMAT_ENCODED;
    struct spa_rectangle size = SPA_RECTANGLE(0, 0);
    if (media_subtype == SPA_MEDIA_SUBTYPE_raw) {
        struct spa_video_info_raw info;
        if (spa_format_video_raw_parse(param, &info) < 0)
            return;
        got = info.format;
        size = info.size;
    } else if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_Format, NULL, SPA_FORMAT_VIDEO_size,
                                     SPA_POD_Rectangle(&size)) < 0) {
        return;
    }

    const struct pwf_filter_video_port_state* want = &port->config.video;
    if (got == want->format && (int)size.width == want->width && (int)size.height == want->height)
        return;

    pwf_log_warning("filter '%s': port negotiated %s %ux%u, not the %s %dx%d it requested; the source "
                    "is shared and was already configured",
                    filter->name ? filter->name : "pwf-filter", pwf_spa_pixel_format_name(got), size.width,
                    size.height, pwf_spa_pixel_format_name(want->format), want->width, want->height);
}

void pwf_filter_on_param_changed(void* data, void* port_data, uint32_t id, const struct spa_pod* param)
{
    struct pwf_filter* filter = data;
    struct pwf_filter_port* port = port_data;

    if (id != SPA_PARAM_Format || !port)
        return;

    if (param != NULL) {
        /* Format negotiated. A DMABUF port advertises its DmaBuf Buffers
         * param now (deferred from add time, which crashes 1.0.5); a
         * no-op for every other port. */
        port->loss_reported = false;
        pwf_filter_check_negotiated_video(filter, port, param);
        pwf_filter_dmabuf_update_params(port);
        return;
    }

    /* param == NULL: the port's negotiated format was cleared — its source
     * is gone, or a DMABUF port's source could not provide DMABUF. */
    /* An output port's format also clears when its consumer leaves, which loses no source. */
    if (port->direction != PWF_FILTER_PORT_INPUT || !pwf_filter_claim_source_loss(filter, port))
        return;
    if (port->use_dmabuf)
        pwf_filter_dmabuf_log_unavailable(port);
    else
        pwf_log_warning("filter '%s': a port's source became unavailable",
                        filter->name ? filter->name : "pwf-filter");

    if (filter->error_cb)
        filter->error_cb((pwf_filter_h)filter, (pwf_filter_port_h)port, PWF_ERR_SOURCE_UNAVAILABLE,
                          filter->user_data);
}

bool pwf_filter_claim_source_loss(struct pwf_filter* filter, struct pwf_filter_port* port)
{
    if (filter->state != PWF_FILTER_STATE_RUNNING || port->loss_reported)
        return false;
    port->loss_reported = true;
    return true;
}

bool pwf_filter_refuse_in_callback(const struct pwf_filter* filter, bool loop_thread_too, const char* call)
{
    bool refused = pwf_filter_processing == filter ||
                   (loop_thread_too && filter->conn.loop && pw_thread_loop_in_thread(filter->conn.loop));
    if (refused)
        pwf_log_error("filter '%s': %s() cannot be called from inside this filter's own callbacks",
                      filter->name ? filter->name : "pwf-filter", call);
    return refused;
}

/* Wakes a draining pwf_filter_stop(..., true), waiting on this same flag
 * under filter->conn.loop's lock. */
static void pwf_filter_on_drained(void* data)
{
    struct pwf_filter* filter = data;
    filter->drained = true;
    pw_thread_loop_signal(filter->conn.loop, false);
}

static const struct pw_filter_events pwf_filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .param_changed = pwf_filter_on_param_changed,
    .process = pwf_filter_on_process,
    .drained = pwf_filter_on_drained,
};

bool pwf_filter_add_port_to_list(struct pwf_filter* filter, struct pwf_filter_port* port)
{
    if (filter->n_ports == filter->ports_capacity) {
        size_t new_cap = filter->ports_capacity == 0 ? 4 : filter->ports_capacity * 2;
        struct pwf_filter_port** grown = realloc(filter->ports, new_cap * sizeof(*grown));
        if (!grown)
            return false;
        filter->ports = grown;
        filter->ports_capacity = new_cap;
    }
    filter->ports[filter->n_ports++] = port;
    return true;
}

static void pwf_filter_teardown(struct pwf_filter* filter)
{
    if (!filter)
        return;

    if (filter->conn.loop && filter->pw_filter) {
        pw_thread_loop_lock(filter->conn.loop);
        pw_filter_destroy(filter->pw_filter);
        filter->pw_filter = NULL;
        pw_thread_loop_unlock(filter->conn.loop);
    } else if (filter->pw_filter) {
        pw_filter_destroy(filter->pw_filter);
        filter->pw_filter = NULL;
    }

    pwf_pw_core_teardown(&filter->conn);
}

pwf_filter_h pwf_filter_create(const char* name, pwf_filter_process_cb callback, void* user_data)
{
    if (!callback)
        return NULL;

    pwf_pw_global_init();

    struct pwf_filter* filter = calloc(1, sizeof(*filter));
    if (!filter) {
        pwf_pw_global_deinit();
        return NULL;
    }

    filter->state = PWF_FILTER_STATE_CREATED;
    filter->process_cb = callback;
    filter->user_data = user_data;

    if (name && *name) {
        filter->name = strdup(name);
        if (!filter->name) {
            free(filter);
            pwf_pw_global_deinit();
            return NULL;
        }
    }

    if (pwf_pw_core_connect(&filter->conn, "pwf-filter-loop") < 0) {
        pwf_filter_teardown(filter);
        free(filter->name);
        free(filter);
        pwf_pw_global_deinit();
        return NULL;
    }

    pw_thread_loop_lock(filter->conn.loop);
    /* Linking a port is optional, so ports may stay unlinked forever;
     * node.always-process keeps .process() firing regardless so the app can
     * drive I/O via push_port_data. */
    struct pw_properties* props = pw_properties_new(PW_KEY_NODE_ALWAYS_PROCESS, "true", NULL);
    /* PipeWire names an unnamed node after the process, so the name has to be
     * set here for other nodes and pwf_filter_port_link() to find it. */
    if (props && filter->name)
        pw_properties_set(props, PW_KEY_NODE_NAME, filter->name);
    filter->pw_filter =
        pw_filter_new(filter->conn.core, filter->name ? filter->name : "pwf-filter", props);
    if (!filter->pw_filter) {
        pw_thread_loop_unlock(filter->conn.loop);
        pwf_log_error("filter '%s': failed to create pipewire filter", filter->name ? filter->name : "pwf-filter");
        pwf_filter_teardown(filter);
        free(filter->name);
        free(filter);
        pwf_pw_global_deinit();
        return NULL;
    }

    pw_filter_add_listener(filter->pw_filter, &filter->filter_listener, &pwf_filter_events, filter);
    pw_thread_loop_unlock(filter->conn.loop);

    /* Ports need a created filter, so no push can reach the lock before this. */
    pthread_mutex_init(&filter->push_lock, NULL);
    return (pwf_filter_h)filter;
}

int pwf_filter_set_error_cb(pwf_filter_h handle, pwf_filter_error_cb callback)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter)
        return PWF_ERR_INVALID_ARG;

    filter->error_cb = callback;
    return PWF_OK;
}

int pwf_filter_set_period_hint(pwf_filter_h handle, uint32_t max_period_ns)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter)
        return PWF_ERR_INVALID_ARG;
    if (filter->state != PWF_FILTER_STATE_CREATED)
        return PWF_ERR_INVALID_ARG;

    filter->period_hint_ns = max_period_ns;
    return PWF_OK;
}

/* Numerator of the node.latency "num/48000" time ratio for a period hint,
 * floored (never coarser than requested) and clamped to at least 1. The
 * denominator 48000 is an arbitrary reference — PipeWire rescales the ratio
 * to the actual graph clock, so no real sample rate is assumed. */
uint32_t pwf_filter_period_hint_num(uint32_t period_ns)
{
    uint64_t num = (uint64_t)period_ns * 48000u / 1000000000ULL;
    return num < 1 ? 1u : (uint32_t)num;
}

/* Applies the period hint as a node.latency preference; a no-op when unset.
 * PipeWire rescales the ratio to the graph clock. */
static void pwf_filter_apply_period_hint(struct pwf_filter* filter)
{
    if (filter->period_hint_ns == 0)
        return;

    char latency[32];
    snprintf(latency, sizeof(latency), "%u/48000", pwf_filter_period_hint_num(filter->period_hint_ns));

    struct spa_dict_item items[] = { SPA_DICT_ITEM_INIT(PW_KEY_NODE_LATENCY, latency) };
    struct spa_dict dict = SPA_DICT_INIT(items, 1);
    pw_filter_update_properties(filter->pw_filter, NULL, &dict);
}

int pwf_filter_start(pwf_filter_h handle)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter)
        return PWF_ERR_INVALID_ARG;
    if (pwf_filter_refuse_in_callback(filter, false, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (filter->n_ports == 0)
        return PWF_ERR_NOT_CONFIGURED;

    pw_thread_loop_lock(filter->conn.loop);
    if (filter->state == PWF_FILTER_STATE_CREATED) {
        /* First start: ports were already added with their format params,
         * so connecting now negotiates and activates them together. The
         * period hint (if any) is a node property, so it must be set before
         * connect. */
        pwf_filter_apply_period_hint(filter);
        int res = pw_filter_connect(filter->pw_filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0);
        if (res < 0) {
            pw_thread_loop_unlock(filter->conn.loop);
            pwf_log_error("filter '%s': failed to connect (result=%d)", filter->name ? filter->name : "pwf-filter", res);
            return PWF_ERR_CONNECT_FAILED;
        }
    } else {
        pw_filter_set_active(filter->pw_filter, true);
    }
    pw_thread_loop_unlock(filter->conn.loop);

    filter->state = PWF_FILTER_STATE_RUNNING;
    return PWF_OK;
}

int pwf_filter_stop(pwf_filter_h handle, bool drain)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter)
        return PWF_ERR_INVALID_ARG;
    if (pwf_filter_refuse_in_callback(filter, drain, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (filter->state != PWF_FILTER_STATE_RUNNING)
        return PWF_OK;

    pw_thread_loop_lock(filter->conn.loop);

    if (drain) {
        /* Must run before the links are dropped below, or a disconnected
         * output port's queued data has nowhere left to go. */
        struct timespec deadline;
        filter->drained = false;
        pw_thread_loop_get_time(filter->conn.loop, &deadline, PWF_FILTER_DRAIN_TIMEOUT_NSEC);
        pw_filter_flush(filter->pw_filter, true);

        while (!filter->drained) {
            if (pw_thread_loop_timed_wait_full(filter->conn.loop, &deadline) < 0) {
                pwf_log_warning("filter '%s': timed out waiting to drain; stopping anyway",
                                filter->name ? filter->name : "pwf-filter");
                break;
            }
        }
    }

    pw_filter_set_active(filter->pw_filter, false);
    /* Return any held buffer to the pool now that processing is paused, and
     * reset per-port hold state so a restart begins holding afresh. */
    for (size_t i = 0; i < filter->n_ports; i++) {
        struct pwf_filter_port* port = filter->ports[i];
        if (port->held) {
            pw_filter_queue_buffer(port, port->held);
            port->held = NULL;
        }
        port->has_held = false;
        port->held_data = NULL;
        port->held_dmabuf_buf = NULL;
        port->current_dmabuf_buf = NULL;
    }
    /* Set before the links go, so the formats they clear are not reported as lost sources. */
    filter->state = PWF_FILTER_STATE_STOPPED;
    pw_thread_loop_unlock(filter->conn.loop);

    /* Links were made against the running graph and a restart re-links
     * explicitly, so they are dropped only now that processing is paused. */
    pwf_filter_release_all_links(filter);
    return PWF_OK;
}

void pwf_filter_destroy(pwf_filter_h handle)
{
    struct pwf_filter* filter = (struct pwf_filter*)handle;
    if (!filter || pwf_filter_refuse_in_callback(filter, true, __func__))
        return;

    if (filter->state == PWF_FILTER_STATE_RUNNING)
        pwf_filter_stop(handle, false);
    else
        pwf_filter_release_all_links(filter);

    /* The registry and any links must go while the loop still runs, since
     * destroying their proxies talks to the server. */
    if (filter->conn.loop) {
        pw_thread_loop_lock(filter->conn.loop);
        pwf_pw_registry_teardown(&filter->registry);
        pw_thread_loop_unlock(filter->conn.loop);
    }

    /* The ports themselves are owned by pw_filter and freed by
     * pw_filter_destroy() inside pwf_filter_teardown(); only the extra
     * heap allocations for each port's push-staging buffer/events are
     * ours, so they must be freed before teardown while the port
     * structs are still valid. */
    for (size_t i = 0; i < filter->n_ports; i++) {
        if (filter->ports[i]) {
            free(filter->ports[i]->pushed_data);
            free(filter->ports[i]->delivered_data);
            pwf_filter_event_free_port(filter->ports[i]);
        }
    }
    free(filter->ports);

    pwf_filter_teardown(filter);

    pthread_mutex_destroy(&filter->push_lock);
    free(filter->name);
    free(filter);
    pwf_pw_global_deinit();
}
