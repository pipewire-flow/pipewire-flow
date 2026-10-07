/* SPDX-License-Identifier: MIT */

#include <stdlib.h>
#include <string.h>

#include <spa/param/param.h>

#include "pwf_log_internal.h"
#include "pwf_stream_internal.h"

_Thread_local const struct pwf_stream* pwf_stream_processing;

bool pwf_stream_refuse_in_callback(const struct pwf_stream* stream, bool loop_thread_too, const char* call)
{
    bool refused = pwf_stream_processing == stream ||
                   (loop_thread_too && stream->conn.loop && pw_thread_loop_in_thread(stream->conn.loop));
    if (refused)
        pwf_log_error("stream: %s() cannot be called from inside this stream's own callbacks", call);
    return refused;
}

/* How long pwf_stream_stop(..., true) waits for a flush to actually drain
 * before giving up and stopping anyway. */
#define PWF_STREAM_DRAIN_TIMEOUT_NSEC (5 * SPA_NSEC_PER_SEC)

void pwf_stream_on_state_changed(void* data, enum pw_stream_state old, enum pw_stream_state state,
                                  const char* error)
{
    struct pwf_stream* stream = data;
    (void)error;

    bool lost_source = (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) &&
                        (old == PW_STREAM_STATE_STREAMING || old == PW_STREAM_STATE_PAUSED);

    if (lost_source && stream->state == PWF_STREAM_STATE_RUNNING) {
        stream->state = PWF_STREAM_STATE_STOPPED;
        pwf_log_warning("stream: %s became unavailable",
                        stream->direction == PWF_STREAM_DIRECTION_PLAYBACK ? "output device" : "source");
        if (stream->error_cb)
            stream->error_cb((pwf_stream_h)stream, PWF_ERR_SOURCE_UNAVAILABLE, stream->user_data);
    }

    /* A stream's node id is only assigned once the server has seen it, so
     * pwf_stream_link() waits on this. */
    pw_thread_loop_signal(stream->conn.loop, false);
}

/* Only a DMABUF-opted stream negotiates a second param after the format,
 * so this is a no-op for every other stream. On success it pushes the
 * deferred Buffers request; on failure it reports the same way a lost
 * source already does. */
static void pwf_stream_on_param_changed(void* data, uint32_t id, const struct spa_pod* param)
{
    struct pwf_stream* stream = data;
    if (id != SPA_PARAM_Format || !stream->use_dmabuf)
        return;

    if (param != NULL) {
        pwf_stream_dmabuf_update_params(stream);
        return;
    }

    pwf_stream_dmabuf_log_unavailable(stream);
    if (stream->state == PWF_STREAM_STATE_RUNNING) {
        stream->state = PWF_STREAM_STATE_STOPPED;
        if (stream->error_cb)
            stream->error_cb((pwf_stream_h)stream, PWF_ERR_SOURCE_UNAVAILABLE, stream->user_data);
    }
}

/* Wakes a draining pwf_stream_stop(..., true), waiting on this same flag
 * under stream->conn.loop's lock. Shared by both directions: PipeWire
 * drains "played or recorded" data alike. */
static void pwf_stream_on_drained(void* data)
{
    struct pwf_stream* stream = data;
    stream->drained = true;
    pw_thread_loop_signal(stream->conn.loop, false);
}

static const struct pw_stream_events pwf_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = pwf_stream_on_state_changed,
    .param_changed = pwf_stream_on_param_changed,
    .process = pwf_stream_on_process,
    .drained = pwf_stream_on_drained,
};

static const struct pw_stream_events pwf_stream_playback_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = pwf_stream_on_state_changed,
    .process = pwf_stream_on_process_playback,
    .drained = pwf_stream_on_drained,
};

static void pwf_stream_teardown(struct pwf_stream* stream)
{
    if (!stream)
        return;

    /* See pwf_stream_internal_connect(): destroying pw_stream can itself
     * fire param_changed with a cleared format, which must not read as a
     * DMABUF failure during ordinary teardown. */
    stream->use_dmabuf = false;

    if (stream->conn.loop && stream->pw_stream) {
        pw_thread_loop_lock(stream->conn.loop);
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->conn.loop);
    } else if (stream->pw_stream) {
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
    }

    pwf_pw_core_teardown(&stream->conn);
}

/* Allocates a stream of `type`/`direction` and brings up its own loop.
 * Callers attach the direction-appropriate callback to the result. */
static struct pwf_stream* pwf_stream_alloc(pwf_data_type type, enum pwf_stream_direction direction,
                                            void* user_data)
{
    pwf_pw_global_init();

    struct pwf_stream* stream = calloc(1, sizeof(*stream));
    if (!stream) {
        pwf_pw_global_deinit();
        return NULL;
    }

    stream->type = type;
    stream->direction = direction;
    stream->state = PWF_STREAM_STATE_CREATED;
    stream->user_data = user_data;
    stream->autoconnect = true; /* the session manager wires us unless told otherwise */

    if (pwf_pw_core_connect(&stream->conn, "pwf-stream-loop") < 0) {
        pwf_stream_teardown(stream);
        free(stream);
        pwf_pw_global_deinit();
        return NULL;
    }

    return stream;
}

pwf_stream_h pwf_stream_create(pwf_data_type type, pwf_stream_data_cb callback, void* user_data)
{
    if (!callback || (type != PWF_DATA_AUDIO && type != PWF_DATA_VIDEO))
        return NULL;

    struct pwf_stream* stream = pwf_stream_alloc(type, PWF_STREAM_DIRECTION_CAPTURE, user_data);
    if (!stream)
        return NULL;

    stream->data_cb = callback;
    return (pwf_stream_h)stream;
}

pwf_stream_h pwf_stream_create_playback(pwf_stream_playback_cb callback, void* user_data)
{
    if (!callback)
        return NULL;

    struct pwf_stream* stream =
        pwf_stream_alloc(PWF_DATA_AUDIO, PWF_STREAM_DIRECTION_PLAYBACK, user_data);
    if (!stream)
        return NULL;

    stream->playback_cb = callback;
    return (pwf_stream_h)stream;
}

int pwf_stream_internal_connect(struct pwf_stream* stream, const struct spa_pod** params, uint32_t n_params,
                                 bool use_dmabuf)
{
    if (stream->pw_stream) {
        /* Destroying a connected pw_stream can itself fire param_changed
         * with a cleared format; clearing this first keeps that read as
         * ordinary teardown rather than a DMABUF failure. */
        stream->use_dmabuf = false;
        pw_thread_loop_lock(stream->conn.loop);
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->conn.loop);
    }

    bool playback = stream->direction == PWF_STREAM_DIRECTION_PLAYBACK;
    const char* media_type = (stream->type == PWF_DATA_AUDIO) ? "Audio" : "Video";
    struct pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, media_type, PW_KEY_MEDIA_CATEGORY,
                                                     playback ? "Playback" : "Capture", NULL);
    if (stream->target)
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, stream->target);
    if (stream->role)
        pw_properties_set(props, PW_KEY_MEDIA_ROLE, stream->role);

    pw_thread_loop_lock(stream->conn.loop);

    stream->pw_stream = pw_stream_new(stream->conn.core, "pwf-stream", props);
    if (!stream->pw_stream) {
        pw_thread_loop_unlock(stream->conn.loop);
        pwf_log_error("stream: failed to create pipewire stream");
        return PWF_ERR_CONNECT_FAILED;
    }

    pw_stream_add_listener(stream->pw_stream, &stream->stream_listener,
                            playback ? &pwf_stream_playback_events : &pwf_stream_events, stream);

    /* Without AUTOCONNECT the node carries no node.autoconnect property, which
     * is what tells a session manager to leave the wiring to us. A DMABUF
     * buffer is not CPU-mapped, so MAP_BUFFERS is omitted for it. */
    enum pw_stream_flags flags = PW_STREAM_FLAG_RT_PROCESS;
    if (!use_dmabuf)
        flags |= PW_STREAM_FLAG_MAP_BUFFERS;
    if (stream->autoconnect)
        flags |= PW_STREAM_FLAG_AUTOCONNECT;

    int res = pw_stream_connect(stream->pw_stream, playback ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT,
                                 PW_ID_ANY, flags, params, n_params);
    if (res < 0) {
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->conn.loop);
        pwf_log_error("stream: failed to connect (result=%d)", res);
        return PWF_ERR_CONNECT_FAILED;
    }

    /* Set only now that the new stream exists, still under the lock: no
     * event for it can be dispatched before this line runs. */
    stream->use_dmabuf = use_dmabuf;

    /* Stay paused until pwf_stream_start() is called explicitly. */
    pw_stream_set_active(stream->pw_stream, false);

    pw_thread_loop_unlock(stream->conn.loop);
    return PWF_OK;
}

/* Replaces an optional string setting with a copy of `value`, where NULL or
 * "" clears it; the old value is kept if the copy cannot be made. */
static int pwf_stream_replace_string(char** slot, const char* value)
{
    char* copy = NULL;
    if (value && *value) {
        copy = strdup(value);
        if (!copy)
            return PWF_ERR_NO_MEMORY;
    }

    free(*slot);
    *slot = copy;
    return PWF_OK;
}

int pwf_stream_set_error_cb(pwf_stream_h handle, pwf_stream_error_cb callback)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;

    stream->error_cb = callback;
    return PWF_OK;
}

int pwf_stream_set_autoconnect(pwf_stream_h handle, bool enable)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;
    /* The routing mode is fixed once the format has connected the stream. */
    if (stream->pw_stream)
        return PWF_ERR_INVALID_ARG;
    /* A target is a hint to the session manager, so it means nothing once the
     * application takes the wiring over. */
    if (!enable && stream->target)
        return PWF_ERR_INVALID_ARG;

    stream->autoconnect = enable;
    return PWF_OK;
}

int pwf_stream_set_target(pwf_stream_h handle, const char* target)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;
    if (target && *target && !stream->autoconnect)
        return PWF_ERR_INVALID_ARG; /* see pwf_stream_set_autoconnect() */

    return pwf_stream_replace_string(&stream->target, target);
}

int pwf_stream_set_role(pwf_stream_h handle, const char* role)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;

    return pwf_stream_replace_string(&stream->role, role);
}

int pwf_stream_start(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, false, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (!stream->format_set || !stream->pw_stream)
        return PWF_ERR_NOT_CONFIGURED;

    pw_thread_loop_lock(stream->conn.loop);
    pw_stream_set_active(stream->pw_stream, true);
    pw_thread_loop_unlock(stream->conn.loop);

    stream->state = PWF_STREAM_STATE_RUNNING;
    return PWF_OK;
}

int pwf_stream_stop(pwf_stream_h handle, bool drain)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, drain, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (stream->state != PWF_STREAM_STATE_RUNNING)
        return PWF_OK;

    pw_thread_loop_lock(stream->conn.loop);

    if (drain) {
        struct timespec deadline;
        stream->drained = false;
        pw_thread_loop_get_time(stream->conn.loop, &deadline, PWF_STREAM_DRAIN_TIMEOUT_NSEC);
        pw_stream_flush(stream->pw_stream, true);

        while (!stream->drained) {
            if (pw_thread_loop_timed_wait_full(stream->conn.loop, &deadline) < 0) {
                pwf_log_warning("stream: timed out waiting to drain; stopping anyway");
                break;
            }
        }
    }

    pw_stream_set_active(stream->pw_stream, false);
    pw_thread_loop_unlock(stream->conn.loop);

    stream->state = PWF_STREAM_STATE_STOPPED;
    return PWF_OK;
}

void pwf_stream_destroy(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream || pwf_stream_refuse_in_callback(stream, true, __func__))
        return;

    if (stream->state == PWF_STREAM_STATE_RUNNING)
        pwf_stream_stop(handle, false);

    /* Destroy releases the wiring; stop deliberately does not, so a stopped
     * stream resumes on the same device. The registry must go while the loop
     * still runs and under its lock, since destroying a proxy talks to the
     * server. */
    pwf_stream_release_links(stream);
    if (stream->conn.loop) {
        pw_thread_loop_lock(stream->conn.loop);
        pwf_pw_registry_teardown(&stream->registry);
        pw_thread_loop_unlock(stream->conn.loop);
    }
    pwf_stream_teardown(stream);
    free(stream->target);
    free(stream->role);
    free(stream);
    pwf_pw_global_deinit();
}
