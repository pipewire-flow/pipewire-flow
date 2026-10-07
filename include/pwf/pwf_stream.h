/* SPDX-License-Identifier: MIT */

/**
 * @file pwf_stream.h
 * @brief Single audio/video capture or audio playback stream, wired
 *        through PipeWire with no session-manager configuration required.
 */

#ifndef PWF_STREAM_H
#define PWF_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pwf/pwf_export.h"
#include "pwf/pwf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque handle to one audio- or video-capture stream. */
struct pwf_stream;

/**
 * @brief One delivered buffer of captured audio samples or a video frame.
 *
 * A struct (rather than loose parameters) so future fields can be added
 * without changing pwf_stream_data_func_t's signature. `data`/`size`/`pts` are
 * valid only for the duration of the data callback.
 */
struct pwf_stream_buffer {
    void* data;  /**< Captured bytes, or NULL when the stream negotiated DMABUF (see pwf_stream_get_dmabuf_planes()). */
    size_t size; /**< Bytes available at `data`; 0 when `data` is NULL. */
    int64_t pts; /**< Capture timestamp in nanoseconds (the driver clock used by the underlying SPA node, e.g. ALSA or V4L2), or -1 if the buffer carried no timestamp metadata. */
};

/**
 * @brief Delivers one buffer of captured audio samples or a video frame.
 *
 * Runs on PipeWire's real-time data thread, so it must not block. Calls that
 * take the stream's loop lock are refused here with PWF_ERR_IN_CALLBACK:
 * start, stop, link, unlink, the target queries and the format setters.
 * pwf_stream_destroy() is refused as well and only logs, so destroy the
 * stream after the callback returns.
 *
 * @param stream    The stream that produced `buf`.
 * @param buf       This cycle's buffer; valid only for the duration of this call.
 * @param user_data The pointer passed to pwf_stream_create().
 */
typedef void (*pwf_stream_data_func_t)(struct pwf_stream* stream, const struct pwf_stream_buffer* buf, void* user_data);

/**
 * @brief One cycle's writable region for a playback stream.
 *
 * `data`, `available` and `pts` are set by the library; the callback sets
 * `size`.
 */
struct pwf_stream_playback_buffer {
    void* data;       /**< Writable region for this cycle, never NULL. */
    size_t available; /**< Bytes the callback may write this cycle: what the device asked for, or the region's full size when the graph states no request. Not the region's capacity — it is usually smaller. */
    int64_t pts;      /**< When this cycle's first sample is expected to be heard, in monotonic nanoseconds, or -1 if the graph cannot state one. The mirror of the capture buffer's pts, not the same thing: capture reports when samples were taken, playback when they will be played. */
    size_t size;      /**< Set by the callback: bytes actually written. Clamped to `available` and floored to whole frames; the remainder up to `available` is emitted as silence, so 0 emits a silent cycle rather than stopping. */
};

/**
 * @brief Asks the application to fill one cycle of audio.
 *
 * Runs on the real-time data thread: it MUST NOT block, allocate, or
 * perform I/O. A cycle whose callback runs past its budget is emitted as
 * silence and recorded in the library log, not reported through
 * pwf_stream_error_func_t. The calls pwf_stream_data_func_t refuses are refused here too.
 *
 * @param stream    The playback stream asking for data.
 * @param buf       This cycle's writable region; valid only for the duration of this call.
 * @param user_data The pointer passed to pwf_stream_create_playback().
 */
typedef void (*pwf_stream_playback_func_t)(struct pwf_stream* stream, struct pwf_stream_playback_buffer* buf,
                                            void* user_data);

/**
 * @brief Reports that `stream`'s source became unavailable while running.
 *
 * Runs on the stream's loop thread. A stop without drain and
 * pwf_stream_unlink() work here, but calls that would wait on this same
 * thread are refused with PWF_ERR_IN_CALLBACK: a draining stop, link,
 * the target queries and the format setters. pwf_stream_destroy() is refused
 * and only logs, so re-route or destroy the stream after the callback returns.
 *
 * @param stream     The affected stream.
 * @param error_code An enum pwf_error, currently always PWF_ERR_SOURCE_UNAVAILABLE.
 * @param user_data  The pointer passed to pwf_stream_create()/pwf_stream_create_playback().
 */
typedef void (*pwf_stream_error_func_t)(struct pwf_stream* stream, int error_code, void* user_data);

/**
 * @brief Creates a stream of `type`.
 *
 * Owns and manages its own PipeWire thread-loop/context/core internally.
 *
 * @param type      PWF_DATA_AUDIO or PWF_DATA_VIDEO; SIGNAL/EVENT are rejected.
 * @param callback  Invoked with each delivered buffer once the stream is started.
 * @param user_data Passed unchanged to `callback`.
 * @return A new stream handle, or NULL if PipeWire cannot be reached or `type` is rejected.
 */
PWF_API struct pwf_stream* pwf_stream_create(enum pwf_data_type type, pwf_stream_data_func_t callback, void* user_data);

/**
 * @brief Creates an audio playback stream, emitting to an output device
 *        instead of capturing from an input one.
 *
 * Audio only: with no media type parameter a video playback stream cannot
 * be expressed. Owns its own PipeWire thread-loop/context/core and fails
 * fast exactly as pwf_stream_create() does. The result is used with the
 * same set_error_callback/set_target/set_audio_config/start/stop/destroy calls;
 * pwf_stream_set_video_config() is rejected on it.
 *
 * @param callback  Invoked once per cycle to fill the next block of audio.
 * @param user_data Passed unchanged to `callback`.
 * @return A new playback stream handle, or NULL if PipeWire cannot be reached.
 */
PWF_API struct pwf_stream* pwf_stream_create_playback(pwf_stream_playback_func_t callback, void* user_data);

/**
 * @brief Registers (or clears, with NULL) the optional async-error callback.
 * @param stream   The stream to configure.
 * @param callback The callback to invoke on source loss, or NULL to clear it.
 * @return PWF_OK, or PWF_ERR_INVALID_ARG for a NULL `stream`.
 */
PWF_API int pwf_stream_set_error_callback(struct pwf_stream* stream, pwf_stream_error_func_t callback);

/**
 * @brief Sets (or clears, with NULL) the PipeWire node this stream should
 *        connect to, by name or serial (as shown by `wpctl status` or
 *        `pw-cli ls Node`).
 *
 * Call it before pwf_stream_set_audio_config()/
 * pwf_stream_set_video_config(), which is what actually connects the
 * stream; a target set after that is kept but never read. If never
 * called, the stream auto-connects to PipeWire's default source for its
 * media type. This is a hint to the session manager, which does the
 * wiring, so it means nothing while autoconnect is off: naming a target
 * then is refused, though clearing with NULL is always accepted.
 *
 * @param stream The stream to target, before its format is set.
 * @param target A node name or object.serial, or NULL to clear a previously set target.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL `stream` or for a non-NULL `target` while autoconnect is off, or PWF_ERR_NO_MEMORY when the name cannot be copied.
 */
PWF_API int pwf_stream_set_target(struct pwf_stream* stream, const char* target);

/**
 * @brief Sets (or clears, with NULL) the media role this stream declares,
 *        such as "Music", "Movie", "Communication" or "Notification".
 *
 * Like a target, it is a hint to the session manager's role policy and is
 * read when pwf_stream_set_audio_config()/pwf_stream_set_video_config()
 * connects the stream, so a role set later does not reach that node.
 * Nothing acts on it without such a policy, which is why it is accepted
 * whatever the autoconnect setting, where a target is not.
 *
 * @param stream The stream to label, before its format is set.
 * @param role   A role name, or NULL to clear a previously set role.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL `stream`, or PWF_ERR_NO_MEMORY when the role cannot be copied.
 */
PWF_API int pwf_stream_set_role(struct pwf_stream* stream, const char* role);

/**
 * @brief One target pwf_stream_set_target()/pwf_stream_link() would
 *        accept, discovered from the running graph.
 */
struct pwf_target_info {
    char name[256];        /**< Node name, usable directly as a target string. */
    char serial[32];       /**< The node's object.serial as decimal digits, also usable directly as a target string — an alternative to `name` when two nodes share one. */
    char description[256]; /**< Human-readable node.description (what wpctl status shows), or "" if the node never set one. */
};

/**
 * @brief Lists targets pwf_stream_set_target()/pwf_stream_link() would
 *        accept for `stream`'s own media type and direction.
 *
 * A capture stream lists sources, a playback stream lists sinks — the
 * same distinction `wpctl status`/`pw-cli ls Node` show as a node's
 * media.class. Requires a live PipeWire connection; a freshly created
 * stream binds the registry and waits for the graph's initial burst of
 * globals on its first call.
 *
 * @param[in]  stream  Selects which targets qualify, by its own type/direction.
 * @param[out] out     Filled with up to `out_len` targets, or NULL to only count them.
 * @param[in]  out_len Capacity of `out`.
 * @param[out] found   The target count actually available, which may exceed `out_len` if it was too small; 0 on failure. A graph with no such node is PWF_OK with 0, not an error.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL `stream` or `found`, PWF_ERR_IN_CALLBACK from inside a callback, PWF_ERR_TIMEOUT when the registry does not answer in time, or PWF_ERR_CONNECT_FAILED when it cannot be reached.
 */
PWF_API int pwf_stream_get_target_list(struct pwf_stream* stream, struct pwf_target_info* out, size_t out_len,
                                        size_t* found);

/**
 * @brief Lists the video formats `target` can deliver to `stream`.
 *
 * Call it between pwf_stream_set_target() and
 * pwf_stream_set_video_config(). Every entry is one that setter accepts
 * for that target, so passing one on cannot fail for want of support.
 * Reading a device's formats opens it briefly, unlike the free lookup
 * pwf_stream_get_target_list() does.
 *
 * @param[in]  stream  A video capture stream, which supplies the connection.
 * @param[in]  target  A node name or object.serial, or NULL for the target already set with pwf_stream_set_target().
 * @param[out] out     Filled with up to `out_len` formats, or NULL to only count them.
 * @param[in]  out_len Capacity of `out`.
 * @param[out] found   The format count actually available, which may exceed `out_len` if it was too small; 0 on failure. A device reporting no format this library can name is PWF_OK with 0, not an error.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL or non-video stream, a NULL `found`, or no target to read, PWF_ERR_NOT_FOUND for a target naming no node, PWF_ERR_IN_CALLBACK from inside a callback, PWF_ERR_TIMEOUT when the query does not answer in time, or PWF_ERR_CONNECT_FAILED when it fails.
 */
PWF_API int pwf_stream_get_target_video_formats(struct pwf_stream* stream, const char* target,
                                                 struct pwf_video_format_info* out, size_t out_len,
                                                 size_t* found);

/**
 * @brief Turns automatic connection off, so the application wires the
 *        stream itself with pwf_stream_link().
 *
 * On by default, which is what every existing caller already gets. Must
 * be called before the format is set; the routing mode is fixed once the
 * stream connects. Mutually exclusive with a target rather than with the
 * setter: turning autoconnect off while one is set is refused, as is
 * naming a target while it is off. Clear the target with
 * pwf_stream_set_target(NULL) to move from either to the other.
 *
 * @param stream The stream to configure, before its format is set.
 * @param enable false to opt out of autoconnect (manual wiring via pwf_stream_link()); true puts the stream back under the session manager, and leaves any target already set in place.
 * @return PWF_OK, or PWF_ERR_INVALID_ARG for a NULL `stream`, a format already set, or `enable` false while a target is set.
 */
PWF_API int pwf_stream_set_autoconnect(struct pwf_stream* stream, bool enable);

/**
 * @brief Connects every channel of `stream` to `target` — a node name or
 *        an object.serial — with PipeWire core links, needing no session
 *        manager.
 *
 * Channels are paired by position, so a stereo stream reaches a stereo
 * device's two ports without the caller naming any of them.
 *
 * Requires autoconnect to be off, and must be called after
 * pwf_stream_start(): the target and the stream's own ports are both
 * resolved in the running graph, and the ports appear shortly after the
 * stream starts. Blocks until every link negotiates; on any channel
 * failing, none is left behind.
 *
 * A device offering more channels than the stream has is not an error:
 * the surplus stay unconnected and the condition is logged, since a mono
 * stream reaching one side of a stereo device looks like a fault. A
 * device offering fewer is rejected outright.
 *
 * @param stream The running stream to wire, with autoconnect off.
 * @param target A node name or an object.serial.
 * @return PWF_OK, or an enum pwf_error: NOT_CONFIGURED before start, INVALID_ARG for a bad mode, target string or channel count or an already-linked stream, NOT_FOUND for a target naming no node, IN_CALLBACK from inside a callback, TIMEOUT when the stream's ports or a link do not appear in time, INVALID_FORMAT when a link fails to negotiate, NO_MEMORY when an allocation fails, CONNECT_FAILED when a link cannot be created.
 */
PWF_API int pwf_stream_link(struct pwf_stream* stream, const char* target);

/**
 * @brief Releases every link created on `stream`.
 *
 * pwf_stream_destroy() does this itself, so an explicit call is only
 * needed to re-target a stream. pwf_stream_stop() deliberately does NOT
 * release: a stopped stream resumes on the same device when started
 * again.
 *
 * @param stream The stream to unlink.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL `stream`, PWF_ERR_NOT_CONFIGURED when the stream has no links, or PWF_ERR_IN_CALLBACK from its data callback.
 */
PWF_API int pwf_stream_unlink(struct pwf_stream* stream);

/**
 * @brief Configures audio format before starting an audio stream.
 * @param stream The stream to configure; must not have started yet.
 * @param config The requested sample rate, channel count, and sample format.
 * @return PWF_OK, PWF_ERR_INVALID_ARG (NULL stream/config, or not an audio stream), PWF_ERR_INVALID_FORMAT (unrecognized format or out-of-range field), PWF_ERR_IN_CALLBACK (from inside a callback), or PWF_ERR_CONNECT_FAILED.
 */
PWF_API int pwf_stream_set_audio_config(struct pwf_stream* stream, const struct pwf_audio_config* config);

/**
 * @brief Configures video format before starting a video stream.
 * @param stream The stream to configure; must not have started yet.
 * @param config The requested width, height, pixel format, and frame rate.
 * @return PWF_OK, PWF_ERR_INVALID_ARG (NULL stream/config, or not a video-capture stream), PWF_ERR_INVALID_FORMAT (unrecognized pixel format or out-of-range dimension), PWF_ERR_IN_CALLBACK (from inside a callback), or PWF_ERR_CONNECT_FAILED.
 */
PWF_API int pwf_stream_set_video_config(struct pwf_stream* stream, const struct pwf_video_config* config);

/**
 * @brief Per-stream DMABUF options.
 *
 * A NULL or zeroed struct means AUTO, i.e. the behavior of
 * pwf_stream_set_video_config().
 */
struct pwf_stream_dmabuf_opts {
    enum pwf_port_memory memory; /**< AUTO (default) or DMABUF. */
    uint32_t reserved[2];        /**< Must be zero; reserved for future options. */
};

/**
 * @brief Configures video format before starting a video capture stream,
 *        with options.
 *
 * Equivalent to pwf_stream_set_video_config() when `opts` is NULL. With
 * opts->memory == PWF_PORT_MEMORY_DMABUF, the stream negotiates DMABUF
 * frames: pwf_stream_buffer.data is NULL for each delivered frame and
 * planes are read via pwf_stream_get_dmabuf_planes(). Video capture only —
 * the same type/direction rejection as pwf_stream_set_video_config()
 * applies, so calling this on an audio or playback stream returns
 * PWF_ERR_INVALID_ARG.
 *
 * If the source cannot provide DMABUF, negotiation fails asynchronously:
 * the condition is logged and pwf_stream_error_func_t (if set) is invoked
 * with PWF_ERR_SOURCE_UNAVAILABLE, the same path used when a
 * source is lost after connecting. The stream delivers no frames until
 * reconfigured without DMABUF.
 *
 * config->pixel_format == "MJPG" combined with DMABUF is rejected right
 * away with PWF_ERR_INVALID_ARG instead: that combination never
 * works, so it fails immediately rather than through the async path above.
 *
 * @param stream The stream to configure; must not have started yet.
 * @param config The requested width, height, pixel format, and frame rate.
 * @param opts   DMABUF options, or NULL for AUTO.
 * @return PWF_OK, PWF_ERR_INVALID_ARG (NULL stream/config, not a video-capture stream, or MJPG requested with DMABUF), PWF_ERR_INVALID_FORMAT, PWF_ERR_IN_CALLBACK (from inside a callback), or PWF_ERR_CONNECT_FAILED.
 */
PWF_API int pwf_stream_set_video_config_ex(struct pwf_stream* stream, const struct pwf_video_config* config,
                                            const struct pwf_stream_dmabuf_opts* opts);

/**
 * @brief Fills up to `planes_len` entries of `planes` with the current
 *        cycle's DMABUF frame layout.
 *
 * Valid only during the data callback (pwf_stream_data_func_t). `planes`
 * comes before `planes_len`, its own capacity, rather than after,
 * keeping the array and its capacity adjacent even though `planes` is
 * the out-parameter here.
 *
 * @param[in]  stream     The stream whose current-cycle buffer to read.
 * @param[out] planes     Filled with up to `planes_len` planes, most-significant plane first.
 * @param[in]  planes_len Capacity of `planes`.
 * @return The plane count actually available, which may exceed `planes_len` if it was too small; 0 for a non-DMABUF stream or a cycle with no buffer, and `planes` is left unwritten.
 */
PWF_API size_t pwf_stream_get_dmabuf_planes(struct pwf_stream* stream, struct pwf_dmabuf_plane* planes,
                                            size_t planes_len);

/**
 * @brief Starts data delivery. Requires a format to already be set.
 * @param stream The stream to start.
 * @return PWF_OK, PWF_ERR_INVALID_ARG (NULL stream), PWF_ERR_IN_CALLBACK (from its data callback), PWF_ERR_NOT_CONFIGURED (no format set), or PWF_ERR_CONNECT_FAILED.
 */
PWF_API int pwf_stream_start(struct pwf_stream* stream);

/**
 * @brief Stops data delivery; the stream may be started again later.
 *
 * With `drain` true, blocks until every buffer already queued has
 * actually been played (a playback stream) or delivered to the data
 * callback (a capture stream), so nothing already queued is lost. If
 * that does not complete within a few seconds (a lost device, for
 * instance), a warning is logged and the stream stops anyway. It is refused
 * from the data callback, and a draining stop from the error callback too.
 *
 * @param stream The stream to stop.
 * @param drain  true to wait for what is already queued to finish first.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL `stream`, or PWF_ERR_IN_CALLBACK for a stop refused inside a callback.
 */
PWF_API int pwf_stream_stop(struct pwf_stream* stream, bool drain);

/**
 * @brief Releases all resources owned by `stream`.
 *
 * Invalid for further use after this call, running or not. Called from inside
 * one of the stream's own callbacks it only logs, since it would tear down the
 * thread running that callback; the stream is left as it was.
 *
 * @param stream The stream to destroy; NULL is a no-op.
 */
PWF_API void pwf_stream_destroy(struct pwf_stream* stream);

#ifdef __cplusplus
}
#endif

#endif /* PWF_STREAM_H */
