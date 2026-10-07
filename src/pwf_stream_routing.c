/* SPDX-License-Identifier: MIT */

#include <stdlib.h>
#include <string.h>

#include "pwf_log_internal.h"
#include "pwf_stream_internal.h"

/* Matches the core connect timeout, as the filter's link does. */
#define PWF_LINK_TIMEOUT_NSEC (5 * SPA_NSEC_PER_SEC)

/* The server publishes a stream's ports shortly after it connects — tens of
 * milliseconds in practice. Waiting is required; how long is not interesting. */
#define PWF_OWN_PORTS_TIMEOUT_NSEC (2 * SPA_NSEC_PER_SEC)

#define PWF_MAX_CHANNELS 64

size_t pwf_stream_pair_ports(size_t n_stream, size_t n_target, size_t* surplus)
{
    if (surplus)
        *surplus = 0;
    if (n_stream == 0 || n_target < n_stream)
        return 0; /* a stream never ends up half-wired */

    if (surplus)
        *surplus = n_target - n_stream;
    return n_stream;
}

/* --- waiting for our own ports --------------------------------------- */

static void pwf_stream_on_port_added(void* data, uint32_t node_id)
{
    struct pwf_stream* stream = data;
    (void)node_id;
    pw_thread_loop_signal(stream->conn.loop, false);
}

static void pwf_stream_on_node_removed(void* data, uint32_t id)
{
    struct pwf_stream* stream = data;

    if (!stream->links || stream->links->target_node_id != id)
        return;

    pwf_log_warning("stream: linked device disappeared");
    pwf_stream_release_links(stream);
    if (stream->error_cb)
        stream->error_cb((pwf_stream_h)stream, PWF_ERR_SOURCE_UNAVAILABLE, stream->user_data);
}

/* How many ports this stream should have once the server has published them. */
static size_t pwf_stream_expected_ports(const struct pwf_stream* stream)
{
    return stream->type == PWF_DATA_AUDIO && stream->format.audio.channels > 0
               ? (size_t)stream->format.audio.channels
               : 1;
}

/* The direction our own ports face: a capture stream reads through inputs,
 * a playback stream writes through outputs. */
static enum spa_direction pwf_stream_own_direction(const struct pwf_stream* stream)
{
    return stream->direction == PWF_STREAM_DIRECTION_PLAYBACK ? SPA_DIRECTION_OUTPUT : SPA_DIRECTION_INPUT;
}

/* Blocks until this stream has a node id and the registry has cached every
 * port it expects, woken by the state-changed and port-added callbacks rather
 * than by polling. Both arrive after start: the id when the server has seen
 * the node, the ports a little after that. */
static int pwf_stream_await_own_ports(struct pwf_stream* stream, size_t expected, uint32_t* out_node_id)
{
    struct timespec deadline;
    enum spa_direction dir = pwf_stream_own_direction(stream);
    uint32_t node_id;

    pw_thread_loop_lock(stream->conn.loop);
    pw_thread_loop_get_time(stream->conn.loop, &deadline, PWF_OWN_PORTS_TIMEOUT_NSEC);

    for (;;) {
        node_id = pw_stream_get_node_id(stream->pw_stream);
        if (node_id != SPA_ID_INVALID &&
            pwf_pw_registry_list_ports(&stream->registry, node_id, dir, NULL, 0) >= expected)
            break;

        if (pw_thread_loop_timed_wait_full(stream->conn.loop, &deadline) < 0) {
            pw_thread_loop_unlock(stream->conn.loop);
            pwf_log_error("stream: timed out waiting for this stream to appear in the graph");
            return PWF_ERR_TIMEOUT;
        }
    }

    pw_thread_loop_unlock(stream->conn.loop);
    *out_node_id = node_id;
    return PWF_OK;
}

/* --- target resolution ------------------------------------------------ */

static bool pwf_str_is_all_digits(const char* s)
{
    if (!s || !*s)
        return false;
    for (const char* p = s; *p; p++)
        if (*p < '0' || *p > '9')
            return false;
    return true;
}

static uint32_t pwf_stream_resolve_target(struct pwf_stream* stream, const char* target)
{
    return pwf_str_is_all_digits(target)
               ? pwf_pw_registry_find_node_by_serial(&stream->registry, strtoull(target, NULL, 10))
               : pwf_pw_registry_find_node_by_name(&stream->registry, target);
}

/* The media.class `stream` should list targets for: sinks for playback,
 * sources for its own audio/video type otherwise. */
static const char* pwf_stream_target_media_class(const struct pwf_stream* stream)
{
    if (stream->direction == PWF_STREAM_DIRECTION_PLAYBACK)
        return "Audio/Sink";
    return stream->type == PWF_DATA_VIDEO ? "Video/Source" : "Audio/Source";
}

int pwf_stream_get_target_list(pwf_stream_h handle, pwf_target_info* out, size_t out_len,
                                size_t* found)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!found)
        return PWF_ERR_INVALID_ARG;

    *found = 0;
    if (!stream)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, true, __func__))
        return PWF_ERR_IN_CALLBACK;
    int bound = pwf_pw_registry_bind(&stream->registry, &stream->conn);
    if (bound < 0)
        return pwf_pw_error_from_sync(bound);

    const char* media_class = pwf_stream_target_media_class(stream);

    pw_thread_loop_lock(stream->conn.loop);
    size_t n = 0;
    for (size_t i = 0; i < stream->registry.n_nodes; i++) {
        const struct pwf_pw_node_entry* e = &stream->registry.nodes[i];
        if (!e->media_class || strcmp(e->media_class, media_class) != 0)
            continue;
        if (out && n < out_len) {
            snprintf(out[n].name, sizeof(out[n].name), "%s", e->name);
            snprintf(out[n].description, sizeof(out[n].description), "%s",
                      e->description ? e->description : "");
            snprintf(out[n].serial, sizeof(out[n].serial), "%llu", (unsigned long long)e->serial);
        }
        n++;
    }
    pw_thread_loop_unlock(stream->conn.loop);

    *found = n;
    return PWF_OK;
}

int pwf_stream_get_target_video_formats(pwf_stream_h handle, const char* target,
                                         pwf_video_format_info* out, size_t out_len,
                                         size_t* found)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!found)
        return PWF_ERR_INVALID_ARG;

    *found = 0;
    if (!stream || stream->type != PWF_DATA_VIDEO)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, true, __func__))
        return PWF_ERR_IN_CALLBACK;

    /* Fall back to the target already set, which is the pairing this call
     * exists for: pick a device, then ask what it can deliver. */
    const char* name = target ? target : stream->target;
    if (!name)
        return PWF_ERR_INVALID_ARG;
    int bound = pwf_pw_registry_bind(&stream->registry, &stream->conn);
    if (bound < 0)
        return pwf_pw_error_from_sync(bound);

    uint32_t node_id = pwf_stream_resolve_target(stream, name);
    if (!node_id) {
        pwf_log_warning("stream: no node named '%s' to read formats from", name);
        return PWF_ERR_NOT_FOUND;
    }

    return pwf_pw_enum_video_formats(&stream->conn, &stream->registry, node_id, out, out_len, found);
}

/* --- link lifecycle --------------------------------------------------- */

static void pwf_stream_link_on_info(void* data, const struct pw_link_info* info)
{
    struct pwf_stream_link* link = data;

    if (!(info->change_mask & PW_LINK_CHANGE_MASK_STATE))
        return;

    if (info->state == PW_LINK_STATE_ACTIVE || info->state == PW_LINK_STATE_PAUSED)
        link->seen_active = true;
    else if (info->state == PW_LINK_STATE_ERROR)
        link->lost = true;

    pw_thread_loop_signal(link->stream->conn.loop, false);
}

static const struct pw_link_events pwf_stream_link_events = {
    PW_VERSION_LINK_EVENTS,
    .info = pwf_stream_link_on_info,
};

void pwf_stream_release_links(struct pwf_stream* stream)
{
    if (!stream || !stream->links)
        return;

    struct pwf_stream_link_set* set = stream->links;
    stream->links = NULL; /* cleared first: destroying a proxy can re-enter */

    pw_thread_loop_lock(stream->conn.loop);
    for (size_t i = 0; i < set->n_links; i++) {
        if (!set->links[i].proxy)
            continue;
        spa_hook_remove(&set->links[i].listener);
        pw_proxy_destroy(set->links[i].proxy);
    }
    pw_thread_loop_unlock(stream->conn.loop);

    free(set->links);
    free(set);
}

/* Creates one link and waits, bounded, for it to negotiate. */
static int pwf_stream_link_one(struct pwf_stream* stream, struct pwf_stream_link* link,
                                uint32_t out_node, uint32_t out_port, uint32_t in_node, uint32_t in_port)
{
    char on[16], op[16], in[16], ip[16];
    snprintf(on, sizeof(on), "%u", out_node);
    snprintf(op, sizeof(op), "%u", out_port);
    snprintf(in, sizeof(in), "%u", in_node);
    snprintf(ip, sizeof(ip), "%u", in_port);

    struct pw_properties* props =
        pw_properties_new(PW_KEY_LINK_OUTPUT_NODE, on, PW_KEY_LINK_OUTPUT_PORT, op,
                          PW_KEY_LINK_INPUT_NODE, in, PW_KEY_LINK_INPUT_PORT, ip, NULL);
    if (!props)
        return PWF_ERR_NO_MEMORY;

    link->stream = stream;
    link->seen_active = false;
    link->lost = false;

    pw_thread_loop_lock(stream->conn.loop);

    link->proxy = pw_core_create_object(stream->conn.core, "link-factory", PW_TYPE_INTERFACE_Link,
                                         PW_VERSION_LINK, &props->dict, 0);
    if (!link->proxy) {
        pw_thread_loop_unlock(stream->conn.loop);
        pw_properties_free(props);
        return PWF_ERR_CONNECT_FAILED;
    }
    pw_proxy_add_object_listener(link->proxy, &link->listener, &pwf_stream_link_events, link);

    struct timespec deadline;
    pw_thread_loop_get_time(stream->conn.loop, &deadline, PWF_LINK_TIMEOUT_NSEC);

    int res = PWF_OK;
    while (!link->seen_active && !link->lost) {
        if (pw_thread_loop_timed_wait_full(stream->conn.loop, &deadline) < 0) {
            res = PWF_ERR_TIMEOUT;
            break;
        }
    }
    if (link->lost)
        res = PWF_ERR_INVALID_FORMAT;

    pw_thread_loop_unlock(stream->conn.loop);
    pw_properties_free(props);
    return res;
}

int pwf_stream_link(pwf_stream_h handle, const char* target)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream || !target || !*target)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, true, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (stream->autoconnect)
        return PWF_ERR_INVALID_ARG; /* two parties would own the wiring */
    if (stream->links)
        return PWF_ERR_INVALID_ARG; /* unlink first to re-target */
    if (stream->state != PWF_STREAM_STATE_RUNNING || !stream->pw_stream)
        return PWF_ERR_NOT_CONFIGURED; /* the graph is where we look things up */

    int bound = pwf_pw_registry_bind(&stream->registry, &stream->conn);
    if (bound < 0)
        return pwf_pw_error_from_sync(bound);
    stream->registry.port_added_cb = pwf_stream_on_port_added;
    stream->registry.port_added_data = stream;
    stream->registry.node_removed_cb = pwf_stream_on_node_removed;
    stream->registry.node_removed_data = stream;

    uint32_t own_node = SPA_ID_INVALID;
    int res = pwf_stream_await_own_ports(stream, pwf_stream_expected_ports(stream), &own_node);
    if (res < 0)
        return res;

    uint32_t target_node = pwf_stream_resolve_target(stream, target);
    if (!target_node) {
        pwf_log_error("stream: no node named '%s' in the graph", target);
        return PWF_ERR_NOT_FOUND;
    }

    /* Monitor ports sit in the opposite direction on both nodes and reuse the
     * same ordinals, so direction is part of the key, not a filter. */
    enum spa_direction own_dir = pwf_stream_own_direction(stream);
    bool we_output = own_dir == SPA_DIRECTION_OUTPUT;
    enum spa_direction peer_dir = we_output ? SPA_DIRECTION_INPUT : SPA_DIRECTION_OUTPUT;

    const struct pwf_pw_port_entry* own[PWF_MAX_CHANNELS];
    const struct pwf_pw_port_entry* peer[PWF_MAX_CHANNELS];
    size_t n_own = pwf_pw_registry_list_ports(&stream->registry, own_node, own_dir, own, PWF_MAX_CHANNELS);
    size_t n_peer =
        pwf_pw_registry_list_ports(&stream->registry, target_node, peer_dir, peer, PWF_MAX_CHANNELS);
    if (n_own > PWF_MAX_CHANNELS || n_peer > PWF_MAX_CHANNELS)
        return PWF_ERR_INVALID_ARG;

    size_t surplus = 0;
    size_t pairs = pwf_stream_pair_ports(n_own, n_peer, &surplus);
    if (pairs == 0) {
        pwf_log_error("stream: '%s' offers %zu channels, this stream needs %zu", target, n_peer, n_own);
        return PWF_ERR_INVALID_ARG;
    }

    struct pwf_stream_link_set* set = calloc(1, sizeof(*set));
    if (!set)
        return PWF_ERR_NO_MEMORY;
    set->links = calloc(pairs, sizeof(*set->links));
    if (!set->links) {
        free(set);
        return PWF_ERR_NO_MEMORY;
    }
    set->target_node_id = target_node;
    stream->links = set;

    for (size_t i = 0; i < pairs; i++) {
        res = pwf_stream_link_one(stream, &set->links[i], we_output ? own_node : target_node,
                                   we_output ? own[i]->id : peer[i]->id, we_output ? target_node : own_node,
                                   we_output ? peer[i]->id : own[i]->id);
        set->n_links = i + 1;
        if (res < 0) {
            /* All or nothing: drop whatever this request already made. */
            pwf_stream_release_links(stream);
            pwf_log_error("stream: linking channel %zu to '%s' failed; released the rest", i, target);
            return res;
        }
        pwf_log_info("stream: linked channel %zu -> %s", i, peer[i]->name);
    }

    if (surplus > 0)
        pwf_log_warning("stream: '%s' has %zu channel(s) this stream does not reach, from %s on",
                        target, surplus, peer[pairs]->name);

    return PWF_OK;
}

int pwf_stream_unlink(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, false, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (!stream->links)
        return PWF_ERR_NOT_CONFIGURED;

    pwf_stream_release_links(stream);
    return PWF_OK;
}
