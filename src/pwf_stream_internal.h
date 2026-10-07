/* SPDX-License-Identifier: MIT */

#ifndef PWF_STREAM_INTERNAL_H
#define PWF_STREAM_INTERNAL_H

#include <stdbool.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>

#include "pwf/pwf_stream.h"
#include "pwf_pw_core_internal.h"

enum pwf_stream_state {
    PWF_STREAM_STATE_CREATED,
    PWF_STREAM_STATE_FORMAT_SET,
    PWF_STREAM_STATE_RUNNING,
    PWF_STREAM_STATE_STOPPED,
};

/* Which way data flows. Decided by which create function was called and
 * fixed for the stream's life; capture is the pre-playback default. */
enum pwf_stream_direction {
    PWF_STREAM_DIRECTION_CAPTURE,
    PWF_STREAM_DIRECTION_PLAYBACK,
};

struct pwf_audio_format_state {
    int sample_rate;
    int channels;
    enum spa_audio_format format;
};

struct pwf_video_format_state {
    int width;
    int height;
    enum spa_video_format format;
};

/* One audio/video capture session, or one audio playback session. */
struct pwf_stream {
    enum pwf_data_type type;
    enum pwf_stream_direction direction;
    enum pwf_stream_state state;
    bool format_set;
    union {
        struct pwf_audio_format_state audio;
        struct pwf_video_format_state video;
    } format;

    /* Playback only: frame stride, and the rate-limit state for the
     * overrun log. Both are meaningless on a capture stream. */
    size_t bytes_per_frame;
    uint64_t overrun_last_log_ns;
    uint64_t overrun_suppressed;
    uint64_t unusable_last_log_ns;
    uint64_t unusable_suppressed;

    /* Set false, then true by .drained, around a draining
     * pwf_stream_stop()'s pw_stream_flush() call. */
    bool drained;

    /* Video capture only, cycle-scoped: current_dmabuf_buf/dmabuf_retrieved
     * are set before the data callback and cleared after. The rate-limit
     * pair below tracks an unretrieved descriptor, not the buffer above. */
    bool use_dmabuf;
    struct spa_buffer* current_dmabuf_buf;
    bool dmabuf_retrieved;
    uint64_t dmabuf_unretrieved_last_log_ns;
    uint64_t dmabuf_unretrieved_suppressed;

    pwf_stream_data_func_t data_cb;
    pwf_stream_playback_func_t playback_cb;
    pwf_stream_error_func_t error_cb;
    void* user_data;

    char* target; /* PW_KEY_TARGET_OBJECT, or NULL for auto-connect */
    char* role;   /* PW_KEY_MEDIA_ROLE, or NULL to declare none */

    /* Manual routing. `autoconnect` is true unless the application said it
     * would wire the stream itself; the rest is unused until it links. */
    bool autoconnect;
    struct pwf_pw_registry registry;
    struct pwf_stream_link_set* links;

    struct pwf_pw_core_conn conn;
    struct pw_stream* pw_stream;

    struct spa_hook stream_listener;
};

/* This is the stream whose data or playback callback this thread is running, or NULL. */
extern _Thread_local const struct pwf_stream* pwf_stream_processing;

/* Refuses a call from inside `stream`'s data or playback callback, and also from its loop
 * thread when `loop_thread_too`, where the call would wait on that thread. Logs `call` on refusal. */
bool pwf_stream_refuse_in_callback(const struct pwf_stream* stream, bool loop_thread_too, const char* call);

/* (Re)connects the underlying pw_stream with the given negotiated format
 * params, destroying any previous one first. `use_dmabuf` omits
 * PW_STREAM_FLAG_MAP_BUFFERS. Must be called with stream->loop unlocked. */
int pwf_stream_internal_connect(struct pwf_stream* stream, const struct spa_pod** params, uint32_t n_params,
                                 bool use_dmabuf);

/* .process callback registered on the underlying pw_stream; dequeues a
 * buffer, hands it to the caller's data_cb, and queues it back. */
void pwf_stream_on_process(void* data);

/* Bytes one frame occupies: the sample size of `format` times `channels`.
 * Returns 0 for an unsupported format or a non-positive channel count. */
size_t pwf_audio_bytes_per_frame(enum spa_audio_format format, int channels);

/* Playback .process callback; dequeues a buffer, sizes the cycle from
 * pw_buffer.requested, fills it via pwf_stream_playback_fill(), publishes. */
void pwf_stream_on_process_playback(void* data);

/* Runs the playback callback over `data`/`available`, then clamps, floors to
 * whole frames and silences the tail. Returns the leading application bytes;
 * the caller publishes the whole cycle, whose tail this has zeroed. */
size_t pwf_stream_playback_fill(struct pwf_stream* stream, void* data, size_t available, int64_t pts);

/* Records one overrun at `now_ns` and reports whether it should be logged;
 * false means it was folded into the suppressed count instead. */
bool pwf_stream_playback_note_overrun(struct pwf_stream* stream, uint64_t now_ns);

/* Current monotonic time in nanoseconds (CLOCK_MONOTONIC). */
uint64_t pwf_monotonic_ns(void);

/* Whether a repeating condition should be logged now rather than folded
 * into `suppressed`: true no more than once per log interval, and always
 * on the first call (`*last_log_ns == 0`). */
bool pwf_rate_limited(uint64_t* last_log_ns, uint64_t* suppressed, uint64_t now_ns);

/* Pushes the DMABUF Buffers param on a DMABUF-opted stream via
 * pw_stream_update_params(); a no-op otherwise. Called from
 * param_changed once the stream's format is set. */
void pwf_stream_dmabuf_update_params(struct pwf_stream* stream);

/* Logs (WARNING) that a DMABUF stream's source could not provide DMABUF,
 * so the stream delivers no frames. A no-op for a non-DMABUF stream. */
void pwf_stream_dmabuf_log_unavailable(struct pwf_stream* stream);

/* One stream channel joined to one device port. */
struct pwf_stream_link {
    struct pw_proxy* proxy;
    struct spa_hook listener;
    struct pwf_stream* stream;
    bool seen_active;
    bool lost;
};

/* Every link joining one stream to one device, created and released as a
 * unit. NULL on the stream means unlinked. */
struct pwf_stream_link_set {
    struct pwf_stream_link* links;
    size_t n_links;
    uint32_t target_node_id;
};

/* Pairs `n_stream` stream ports with `n_target` device ports by position.
 * Returns the number of pairs to create, or 0 when the device cannot satisfy
 * the stream; `surplus` receives how many device ports are left over. */
size_t pwf_stream_pair_ports(size_t n_stream, size_t n_target, size_t* surplus);

/* Releases every link on `stream` and clears the set. Safe when unlinked. */
void pwf_stream_release_links(struct pwf_stream* stream);

/* .state_changed callback registered on the underlying pw_stream; detects
 * the source-lost transition and invokes error_cb. Exposed (non-static) so
 * tests can simulate a state transition without a real device. */
void pwf_stream_on_state_changed(void* data, enum pw_stream_state old, enum pw_stream_state state,
                                  const char* error);

#endif /* PWF_STREAM_INTERNAL_H */
