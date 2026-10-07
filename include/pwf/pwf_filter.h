/* SPDX-License-Identifier: MIT */

/**
 * @file pwf_filter.h
 * @brief Multi-port PipeWire filter node: audio/video/signal/event ports
 *        processed together on one real-time callback.
 */

#ifndef PWF_FILTER_H
#define PWF_FILTER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "pwf/pwf_export.h"
#include "pwf/pwf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque handle to one multi-port filter. */
struct pwf_filter;

/** @brief Opaque handle to one input or output port on a filter. */
struct pwf_filter_port;

/** @brief Direction of a filter port. */
enum pwf_filter_port_direction {
    PWF_FILTER_PORT_INPUT  = 0, /**< Consumes data delivered by the graph, or pushed with pwf_filter_push_port_data()/pwf_filter_port_push_event(). */
    PWF_FILTER_PORT_OUTPUT = 1  /**< Produces data for the graph, written from the processing callback. */
};

/**
 * @brief One port's buffer for a single processing cycle.
 *
 * `data`/`size`/`capacity`/`pts` are valid only for the duration of the
 * process callback.
 */
struct pwf_filter_port_buffer {
    struct pwf_filter_port* port; /**< Which port this entry describes. */
    void* data;   /**< NULL if no buffer was available this cycle. */
    size_t size;  /**< Input: bytes available to read. Output: bytes to publish; set by the callback before returning (0 = no output this cycle). */
    size_t capacity; /**< Output ports only: max bytes `data` can hold. */
    int64_t pts; /**< Input ports only: capture/presentation timestamp in nanoseconds from the source (e.g. an ALSA/V4L2 device's driver clock), or the value passed to pwf_filter_push_port_data() for pushed data. -1 if unavailable. Always -1 on output ports and on event ports (each struct pwf_event carries its own `offset` instead). */
    bool fresh;  /**< Input ports only: true only when this buffer is new this cycle; false for a held re-presentation (see pwf_filter_port_set_hold()) or a cycle with no buffer. Always false on output ports. */
    uint64_t seq; /**< Input ports only: per-port update counter that advances only when new data arrives, so it is unchanged across held cycles and its deltas count genuinely new buffers. */
};

/**
 * @brief Invoked once per processing cycle with every port's buffer.
 *
 * Runs on PipeWire's real-time data thread, so it must not block. The push,
 * event and DMABUF calls belong here, but calls that take the filter's loop
 * lock are refused with PWF_ERR_IN_CALLBACK: start, stop, port link and
 * unlink, and pwf_filter_get_target_video_formats(). pwf_filter_destroy() is
 * refused too and only logs, so destroy the filter after the callback returns.
 *
 * @param filter    The filter running this cycle.
 * @param buffers   One entry per port; valid only for the duration of this call.
 * @param n_buffers Number of entries in `buffers`.
 * @param user_data The pointer passed to pwf_filter_create().
 */
typedef void (*pwf_filter_process_func_t)(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers,
                                           size_t n_buffers, void* user_data);

/**
 * @brief Reports that `port` on `filter` became unavailable while
 *        running; the filter's other ports are unaffected.
 *
 * Runs on the filter's loop thread, once per source an input port loses, and
 * never for the application's own unlink, stop or destroy. A stop without
 * drain and pwf_filter_port_unlink() work here. A draining stop,
 * pwf_filter_port_link() and pwf_filter_get_target_video_formats() would wait
 * on this thread, so they are refused with PWF_ERR_IN_CALLBACK, and
 * pwf_filter_destroy() is refused and only logs.
 *
 * @param filter     The filter owning `port`.
 * @param port       The port that became unavailable.
 * @param error_code An enum pwf_error, currently always PWF_ERR_SOURCE_UNAVAILABLE.
 * @param user_data  The pointer passed to pwf_filter_create().
 */
typedef void (*pwf_filter_error_func_t)(struct pwf_filter* filter, struct pwf_filter_port* port, int error_code,
                                         void* user_data);

/**
 * @brief Which real wire kind an event carries.
 *
 * MIDI/OSC carry real MIDI/OSC wire bytes for interop with other
 * PipeWire MIDI/OSC clients; PROPERTY is the general-purpose kind for
 * anything else a caller writes. UNKNOWN is read-only: it appears only
 * on an event pwf_filter_port_get_event() decoded from a control item
 * this library doesn't recognize (for example, one written by some
 * other PipeWire client using a control kind outside this set); `data`/
 * `size` are that item's raw, undecoded value bytes, and `key` is NULL.
 * Passing UNKNOWN to pwf_filter_port_push_event() is rejected.
 */
enum pwf_event_kind {
    PWF_EVENT_MIDI     = 0, /**< Real MIDI wire bytes. */
    PWF_EVENT_OSC      = 1, /**< Real OSC wire bytes. */
    PWF_EVENT_PROPERTY = 2, /**< General-purpose named value; `key` selects it. */
    PWF_EVENT_UNKNOWN  = 3  /**< Read-only: an undecoded control item from another client. */
};

/**
 * @brief One discrete, time-stamped item exchanged through an event port.
 *
 * `key` is meaningful only for PWF_EVENT_PROPERTY (a name from the
 * supported property vocabulary) and MUST be NULL otherwise. Pointers
 * are valid only for the duration of the processing callback (when read
 * from pwf_filter_port_get_event()) or the call to
 * pwf_filter_port_push_event() (the library copies what it needs from a
 * pushed event).
 */
struct pwf_event {
    uint32_t offset;     /**< This event's position within the current cycle, in frames. */
    enum pwf_event_kind kind; /**< MIDI, OSC, PROPERTY, or (read-only) UNKNOWN. */
    const char* key;     /**< Property name for PWF_EVENT_PROPERTY; NULL otherwise. */
    const void* data;   /**< MIDI/OSC: real wire-format bytes. PROPERTY: the value's raw bytes. UNKNOWN: the raw undecoded control value's bytes. */
    size_t size;        /**< Bytes at `data`. */
};

/**
 * @brief Creates an empty filter (no ports yet) whose node other
 *        applications and pwf_filter_port_link() find by `name`.
 *
 * Internally owns and manages its own PipeWire thread-loop/context/core.
 *
 * @param name      The node's name (node.name); NULL or empty leaves PipeWire's default, the process name.
 * @param callback  Invoked once per processing cycle after pwf_filter_start().
 * @param user_data Passed unchanged to `callback`.
 * @return A new filter handle, or NULL if PipeWire cannot be reached.
 */
PWF_API struct pwf_filter* pwf_filter_create(const char* name, pwf_filter_process_func_t callback, void* user_data);

/**
 * @brief Registers (or clears, with NULL) the optional per-port
 *        async-error callback.
 * @param filter   The filter to configure.
 * @param callback The callback to invoke on port loss, or NULL to clear it.
 * @return PWF_OK, or PWF_ERR_INVALID_ARG for a NULL `filter`.
 */
PWF_API int pwf_filter_set_error_callback(struct pwf_filter* filter, pwf_filter_error_func_t callback);

/**
 * @brief Adds one audio port (input or output) to `filter`.
 *
 * Must be called before pwf_filter_start(); adding ports after starting
 * is unsupported.
 *
 * @param filter    The filter to add the port to, not yet started.
 * @param direction PWF_FILTER_PORT_INPUT or PWF_FILTER_PORT_OUTPUT.
 * @param config    The requested sample rate, channel count, and sample format.
 * @return The new port handle, or NULL on invalid arguments or an unsupported format.
 */
PWF_API struct pwf_filter_port* pwf_filter_add_audio_port(struct pwf_filter* filter,
                                                          enum pwf_filter_port_direction direction,
                                                          const struct pwf_audio_config* config);

/**
 * @brief Lists the video formats `target` can deliver to a port on `filter`.
 *
 * Call it before pwf_filter_add_video_port(), which fixes a port's format
 * for good. A filter port carries no converter, unlike a pwf_stream, so a
 * format the target lacks only surfaces as PWF_ERR_INVALID_FORMAT
 * from pwf_filter_port_link(). Every entry here is one
 * pwf_filter_add_video_port() accepts. Pass pwf_filter_port_link() the
 * same `target` string; reading formats opens the device briefly.
 *
 * @param[in]  filter  Supplies the connection; need not be started yet.
 * @param[in]  target  A node name, an object.serial, or "node:port"; the port part is accepted and ignored, as formats belong to the node.
 * @param[out] out     Filled with up to `out_len` formats, or NULL to only count them.
 * @param[in]  out_len Capacity of `out`.
 * @param[out] found   The format count actually available, which may exceed `out_len` if it was too small; 0 on failure. A device reporting no format this library can name is PWF_OK with 0, not an error.
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a NULL filter, target or `found`, PWF_ERR_NOT_FOUND for a target naming no node, PWF_ERR_IN_CALLBACK from inside a callback, PWF_ERR_TIMEOUT when the query does not answer in time, or PWF_ERR_CONNECT_FAILED when it fails.
 */
PWF_API int pwf_filter_get_target_video_formats(struct pwf_filter* filter, const char* target,
                                                 struct pwf_video_format_info* out, size_t out_len,
                                                 size_t* found);

/**
 * @brief Adds one video port (input or output) to `filter`.
 *
 * Same timing and failure behavior as pwf_filter_add_audio_port().
 *
 * @param filter    The filter to add the port to, not yet started.
 * @param direction PWF_FILTER_PORT_INPUT or PWF_FILTER_PORT_OUTPUT.
 * @param config    The requested width, height, pixel format, and frame rate.
 * @return The new port handle, or NULL on invalid arguments or an unsupported format.
 */
PWF_API struct pwf_filter_port* pwf_filter_add_video_port(struct pwf_filter* filter,
                                                          enum pwf_filter_port_direction direction,
                                                          const struct pwf_video_config* config);

/**
 * @brief Extensible per-port options.
 *
 * A NULL or zeroed struct means AUTO, i.e. the behavior of the non-_ex
 * add call. enum pwf_port_memory is declared in pwf_stream.h, shared with
 * pwf_stream_get_dmabuf_planes().
 */
struct pwf_filter_port_opts {
    enum pwf_port_memory memory; /**< AUTO (default) or DMABUF. */
    uint32_t reserved[2];        /**< Must be zero; reserved for future options. */
};

/**
 * @brief Adds one video port with options.
 *
 * Equivalent to pwf_filter_add_video_port() when `opts` is NULL. With
 * opts->memory == PWF_PORT_MEMORY_DMABUF the port (input only) negotiates
 * DMABUF frames; its pwf_filter_port_buffer.data is NULL and planes are
 * read via pwf_filter_port_get_dmabuf_planes().
 *
 * @param filter    The filter to add the port to, not yet started.
 * @param direction PWF_FILTER_PORT_INPUT or PWF_FILTER_PORT_OUTPUT.
 * @param config    The requested width, height, pixel format, and frame rate.
 * @param opts      Per-port options, or NULL for AUTO.
 * @return The new port handle, or NULL for DMABUF on an output port or an unsupported/invalid request.
 */
PWF_API struct pwf_filter_port* pwf_filter_add_video_port_ex(struct pwf_filter* filter,
                                                             enum pwf_filter_port_direction direction,
                                                             const struct pwf_video_config* config,
                                                             const struct pwf_filter_port_opts* opts);

/**
 * @brief Fills up to `planes_len` entries of `planes` with the current
 *        cycle's DMABUF frame layout for `buf`.
 *
 * Valid only during the processing callback. `planes` comes before
 * `planes_len`, its own capacity, rather than after, keeping the array
 * and its capacity adjacent even though `planes` is the out-parameter
 * here.
 *
 * @param[in]  buf        The port buffer to read, from this cycle's pwf_filter_process_func_t.
 * @param[out] planes     Filled with up to `planes_len` planes, most-significant plane first.
 * @param[in]  planes_len Capacity of `planes`.
 * @return The plane count actually available, which may exceed `planes_len` if it was too small; 0 for a non-DMABUF port or a cycle with no buffer, and `planes` is left unwritten.
 */
PWF_API size_t pwf_filter_port_get_dmabuf_planes(const struct pwf_filter_port_buffer* buf,
                                                  struct pwf_dmabuf_plane* planes, size_t planes_len);

/**
 * @brief Enables (or disables) single-buffer "hold" on an input `port`.
 *
 * On a cycle where the port receives no new data, its most recent buffer
 * is re-presented (same DMABUF fd) with pwf_filter_port_buffer.fresh ==
 * false, instead of reporting no buffer. Exactly one buffer is retained.
 * Must be called before pwf_filter_start().
 *
 * @param port   An input port, not yet started.
 * @param enable true to re-present the last buffer on an empty cycle, false to report no buffer instead.
 * @return PWF_OK, or PWF_ERR_INVALID_ARG for a NULL or output port, or a filter already started.
 */
PWF_API int pwf_filter_port_set_hold(struct pwf_filter_port* port, bool enable);

/**
 * @brief Records a preferred maximum bundling period in nanoseconds,
 *        offered to the graph as a requested latency at connect time.
 *
 * A duration, which PipeWire rescales to the graph clock; it never
 * forces the graph's clock or driver. 0 clears the hint. Must be called
 * before pwf_filter_start().
 *
 * @param filter        The filter to configure, not yet started.
 * @param max_period_ns Preferred maximum bundling period in nanoseconds, or 0 to clear the hint.
 * @return PWF_OK, or PWF_ERR_INVALID_ARG for a NULL filter or one already started.
 */
PWF_API int pwf_filter_set_period_hint(struct pwf_filter* filter, uint32_t max_period_ns);

/**
 * @brief Links an input `port` straight to a source node, needing no
 *        external tool or session manager.
 *
 * Must be called after pwf_filter_start(), unlike the other port calls,
 * because the target is looked up in the running graph. Blocks until the
 * link negotiates, which is why it is refused from the filter's own callbacks.
 *
 * @param port   An input port on a started filter.
 * @param target A node name, an object.serial, or "node:port"; naming only a node lets PipeWire pick a compatible port.
 * @return PWF_OK, or an enum pwf_error: NOT_CONFIGURED before start, INVALID_ARG for a bad port or target string or an already-linked port, NOT_FOUND for a target naming no node or port, IN_CALLBACK from inside a callback, INVALID_FORMAT when the link fails to negotiate, TIMEOUT when it does not negotiate in time, NO_MEMORY when an allocation fails, CONNECT_FAILED when the link cannot be created.
 */
PWF_API int pwf_filter_port_link(struct pwf_filter_port* port, const char* target);

/**
 * @brief Releases the link created on `port`, which is only needed to
 *        re-target it while running; stop and destroy release every
 *        link themselves.
 * @param port The linked port to unlink.
 * @return PWF_OK, or an enum pwf_error when the port has no link or the call comes from the process callback.
 */
PWF_API int pwf_filter_port_unlink(struct pwf_filter_port* port);

/**
 * @brief Adds one signal port (input or output) to `filter` — a
 *        continuous channel of raw 32-bit float values, one value per
 *        frame of each processing cycle (matching how audio port
 *        buffers are sized).
 *
 * No format configuration is needed. Same timing and failure behavior
 * as pwf_filter_add_audio_port().
 *
 * @param filter    The filter to add the port to, not yet started.
 * @param direction PWF_FILTER_PORT_INPUT or PWF_FILTER_PORT_OUTPUT.
 * @return The new port handle, or NULL on invalid arguments.
 */
PWF_API struct pwf_filter_port* pwf_filter_add_signal_port(struct pwf_filter* filter,
                                                           enum pwf_filter_port_direction direction);

/**
 * @brief Adds one event port (input or output) to `filter` — carries
 *        zero or more discrete struct pwf_event items per processing cycle
 *        instead of a raw buffer.
 *
 * No format configuration is needed. Same timing and failure behavior
 * as pwf_filter_add_audio_port().
 *
 * @param filter    The filter to add the port to, not yet started.
 * @param direction PWF_FILTER_PORT_INPUT or PWF_FILTER_PORT_OUTPUT.
 * @return The new port handle, or NULL on invalid arguments.
 */
PWF_API struct pwf_filter_port* pwf_filter_add_event_port(struct pwf_filter* filter,
                                                          enum pwf_filter_port_direction direction);

/**
 * @brief Returns the media kind `port` was added with (AUDIO/VIDEO/
 *        SIGNAL/EVENT).
 *
 * Valid for any port handle obtained from any add_*_port() call.
 *
 * @param port The port to query.
 * @return The enum pwf_data_type `port` was added as.
 */
PWF_API enum pwf_data_type pwf_filter_port_get_type(struct pwf_filter_port* port);

/**
 * @brief Returns the number of events available on `port` (an input
 *        event port) for the current processing cycle.
 *
 * Valid only during the processing callback.
 *
 * @param port An input event port.
 * @return The event count for this cycle; 0 if none.
 */
PWF_API size_t pwf_filter_port_get_event_count(struct pwf_filter_port* port);

/**
 * @brief Reads the event at `index` (0-based, cycle-delivery order) on
 *        `port` (an input event port) into `*out`.
 *
 * An item of a control kind this library doesn't recognize is still
 * returned, as PWF_EVENT_UNKNOWN. Valid only during the processing
 * callback; `out`'s data/key pointers are valid only for that same call.
 *
 * @param[in]  port  An input event port.
 * @param[in]  index 0-based index into this cycle's delivered events.
 * @param[out] out   Filled with the event at `index`.
 * @return PWF_OK, or an enum pwf_error (invalid index, or wrong port kind/direction).
 */
PWF_API int pwf_filter_port_get_event(struct pwf_filter_port* port, size_t index, struct pwf_event* out);

/**
 * @brief Adds one event to `port`'s event queue; the library copies
 *        `event`'s data.
 *
 * Behavior depends on `port`'s direction:
 *   - Output port: appends to the current processing cycle's outgoing
 *     events, published when the cycle ends. Must be called only from
 *     within the processing callback (its capacity is bounded by that
 *     cycle's negotiated buffer).
 *   - Input port: stages the event for delivery on the filter's next
 *     processing cycle, without creating any PipeWire-level connection
 *     — the event-port equivalent of pwf_filter_push_port_data(), for
 *     application code (for example, a test or another in-process
 *     source) to feed an event port directly. Callable anytime, not
 *     just from within the processing callback.
 *
 * @param port  The event port to push to.
 * @param event The event to copy and enqueue.
 * @return PWF_OK, PWF_ERR_INVALID_ARG (wrong port kind, an invalid/unrecognized PROPERTY key, or — output ports only — no room left in the current cycle's buffer), or PWF_ERR_NO_MEMORY when the event cannot be copied.
 */
PWF_API int pwf_filter_port_push_event(struct pwf_filter_port* port, const struct pwf_event* event);

/**
 * @brief Stages `size` bytes from `data` for `port` (an input port) to
 *        be delivered on the filter's next processing cycle, without
 *        creating any PipeWire-level connection.
 *
 * Lets application code (for example, a capture stream's data callback)
 * feed a filter directly. Only the most recently pushed buffer per port
 * is kept. Not valid for event ports — use pwf_filter_port_push_event()
 * instead. A cycle that begins while another push to the filter is still
 * being copied leaves the staged data to the cycle after it, never waiting.
 *
 * @param filter The filter owning `port`.
 * @param port   An input, non-event port on `filter`.
 * @param data   Bytes to stage; copied by the library.
 * @param size   Bytes at `data`.
 * @param pts    Carried through unchanged to that cycle's pwf_filter_port_buffer.pts; pass -1 if the source has no timestamp (e.g. pwf_stream_data_func_t's own `pts` when bridging a capture stream into a filter).
 * @return PWF_OK, PWF_ERR_INVALID_ARG for a bad filter, port or data, or PWF_ERR_NO_MEMORY when the push buffer cannot grow.
 */
PWF_API int pwf_filter_push_port_data(struct pwf_filter* filter, struct pwf_filter_port* port, const void* data,
                                      size_t size, int64_t pts);

/**
 * @brief Starts processing. Fails if the filter has zero ports.
 *
 * Safe to call again after stop(). It is refused from the process callback.
 *
 * @param filter The filter to start.
 * @return PWF_OK, or an enum pwf_error.
 */
PWF_API int pwf_filter_start(struct pwf_filter* filter);

/**
 * @brief Stops processing; the filter may be restarted via pwf_filter_start().
 *
 * With `drain` true, blocks until every buffer already queued has
 * actually been sent out an output port or handed to the processing
 * callback on an input port, so nothing already queued is lost. If
 * that does not complete within a few seconds, a warning is logged
 * and the filter stops anyway. It is refused from the process callback, and a
 * draining stop from the error callback too.
 *
 * @param filter The filter to stop.
 * @param drain  true to wait for what is already queued to finish first.
 * @return PWF_OK, or an enum pwf_error.
 */
PWF_API int pwf_filter_stop(struct pwf_filter* filter, bool drain);

/**
 * @brief Releases all resources owned by `filter`, including its ports
 *        and its internal thread-loop/context, whether or not it was
 *        running.
 *
 * `filter` and any of its port handles are invalid after this returns.
 * Called from inside one of the filter's own callbacks it only logs, and the
 * filter is left as it was.
 *
 * @param filter The filter to destroy; NULL is a no-op.
 */
PWF_API void pwf_filter_destroy(struct pwf_filter* filter);

#ifdef __cplusplus
}
#endif

#endif /* PWF_FILTER_H */
