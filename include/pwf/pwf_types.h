/* SPDX-License-Identifier: MIT */

/**
 * @file pwf_types.h
 * @brief The data types pwf_stream and pwf_filter share: what a stream or
 *        port carries, how a call failed, and the format descriptions.
 */

#ifndef PWF_TYPES_H
#define PWF_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pwf/pwf_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Classifies a stream, or a pwf_filter_* port, by the kind of data
 *        it carries.
 *
 * Audio and video are media; a signal and an event are not, which is why
 * this is a data type. SIGNAL and EVENT are filter-port-only:
 * pwf_stream_create() only accepts AUDIO/VIDEO and rejects the other two.
 */
enum pwf_data_type {
    PWF_DATA_AUDIO  = 0, /**< Raw audio samples. */
    PWF_DATA_VIDEO  = 1, /**< Raw video frames. */
    PWF_DATA_SIGNAL = 2, /**< One 32-bit float per frame, e.g. a sensor reading; filter ports only, see pwf_filter.h. */
    PWF_DATA_EVENT  = 3  /**< Discrete timestamped items such as MIDI or a property; filter ports only, see pwf_filter.h. */
};

/** @brief Library error codes. Negative values only; 0 is success. */
enum pwf_error {
    PWF_OK                     = 0,  /**< Success. */
    PWF_ERR_INVALID_ARG        = -1, /**< A NULL/out-of-range argument, or a call this kind of stream or port never accepts (e.g. a video format on an audio stream). */
    PWF_ERR_CONNECT_FAILED     = -2, /**< Connecting to PipeWire, or asking it to create a stream, link or query, failed. */
    PWF_ERR_INVALID_FORMAT     = -3, /**< An unrecognized pixel/sample format string, or an out-of-range dimension/rate. */
    PWF_ERR_NOT_CONFIGURED     = -4, /**< Called before a required prior step, e.g. start() before a format was set, link() before start(), or unlink() with nothing linked. */
    PWF_ERR_SOURCE_UNAVAILABLE = -5, /**< The connected source disappeared, or could not provide the requested memory type (see pwf_stream_error_func_t). */
    PWF_ERR_IN_CALLBACK        = -6, /**< Called from inside one of the object's own callbacks, where the call cannot run; call it again after the callback returns. */
    PWF_ERR_NOT_FOUND          = -7, /**< No node in the graph matches the target name or serial; it may have gone away or never existed. */
    PWF_ERR_TIMEOUT            = -8, /**< PipeWire did not answer, or a link did not negotiate, within the library's time limit; the same call may succeed if retried. */
    PWF_ERR_NO_MEMORY          = -9, /**< A memory allocation failed. */
    PWF_ERR_INVALID_STATE      = -10, /**< The arguments are fine, but the object's current state or routing mode rules the call out: a setting changed after connect or start, a second link, a target while autoconnect is off, an output event pushed outside the processing callback. */
    PWF_ERR_NO_SPACE           = -11 /**< The data does not fit where it has to go: an event pushed to an output port exceeds what is left of this cycle's buffer, or the port has no buffer this cycle. A later cycle may have room, unless the event is larger than the port's whole buffer. */
};

/**
 * @brief Describes a library error code in a short English phrase.
 *
 * Meant for log and diagnostic output, e.g. alongside the value an API
 * call returned or an error callback received.
 *
 * @param error An enum pwf_error value, as returned by any pwf_ call.
 * @return A static string, never NULL; "Unknown error" for a value outside enum pwf_error. Do not free it.
 */
PWF_API const char* pwf_strerror(int error);

/**
 * @brief Audio capture configuration passed to
 *        pwf_stream_set_audio_config() and pwf_filter_add_audio_port().
 */
struct pwf_audio_config {
    int sample_rate;    /**< Hz, e.g. 48000. */
    int channels;       /**< Channel count, e.g. 2. */
    const char* format; /**< "U8", "S16", "S24", "S24_32", "S32", or "F32"; NULL defaults to "S16". */
};

/** @brief Video capture configuration passed to pwf_stream_set_video_config(). */
struct pwf_video_config {
    int width;                /**< Frame width in pixels. */
    int height;               /**< Frame height in pixels. */
    const char* pixel_format; /**< "RGB", "YUYV", "NV12", "NV21", "I420", "MJPG", or "H264". MJPG and H264 are compressed: each delivered frame's size varies, and DMABUF delivery is not available for them. */
    int fps;                  /**< Frames per second; 0 negotiates automatically. */
};

/**
 * @brief Selects the memory backing a stream's or port's buffers.
 *
 * AUTO is the default (graph-selected, normally CPU-mapped). DMABUF
 * negotiates file descriptors instead of a CPU buffer; only video accepts
 * it today.
 */
enum pwf_buffer_memory {
    PWF_BUFFER_MEMORY_AUTO   = 0, /**< Graph-selected, normally CPU-mapped. */
    PWF_BUFFER_MEMORY_DMABUF = 1  /**< Negotiate DMABUF file descriptors. */
};

/**
 * @brief How a stream or port wants its buffers delivered, as opposed to
 *        the media format its config struct describes.
 *
 * Shared by pwf_stream_set_video_config_ex() and
 * pwf_filter_add_video_port_ex(). A NULL or zeroed struct means AUTO, the
 * behavior of the matching non-_ex call. Zero-initialize it: anything this
 * version does not define is rejected, which leaves those values free for
 * later versions.
 */
struct pwf_buffer_opts {
    enum pwf_buffer_memory memory; /**< AUTO (default) or DMABUF; any other value is rejected. */
    uint32_t flags;                /**< Must be zero, or the call is rejected; reserved for future flags. */
    uint64_t reserved[4];          /**< Must be zero, or the call is rejected; reserved for future options. */
};

/**
 * @brief One plane of a DMABUF-delivered frame.
 *
 * `fd` is borrowed (import-only, not owned): valid only for the callback
 * that received it, do not close it.
 */
struct pwf_dmabuf_plane {
    int      fd;     /**< Borrowed DMABUF file descriptor; do not close. */
    uint32_t offset; /**< Byte offset to the plane within the dmabuf. */
    uint32_t stride; /**< Row stride in bytes. */
    uint32_t size;   /**< Valid bytes of this plane. */
};

/**
 * @brief One pixel format and frame size a target can deliver.
 *
 * Every field goes straight into a struct pwf_video_config; a pixel format this
 * library cannot name is left out rather than reported as unusable. A
 * device taking a range of sizes reports its ends, so `width_max`/
 * `height_max` exceed `width`/`height` there and equal them otherwise.
 */
struct pwf_video_format_info {
    char   pixel_format[16]; /**< "RGB", "YUYV", "NV12", "NV21", "I420", "MJPG", or "H264", as struct pwf_video_config takes it. */
    int    width;            /**< Frame width in pixels, or the smallest one for a size range. */
    int    height;           /**< Frame height in pixels, or the smallest one for a size range. */
    int    width_max;        /**< Equal to `width` for a discrete size, the range's largest width otherwise. */
    int    height_max;       /**< Equal to `height` for a discrete size, the range's largest height otherwise. */
    int    fps[8];           /**< Frame rates at this size, highest first; whole frames per second, as struct pwf_video_config takes them. */
    size_t n_fps;            /**< Entries set in `fps`, never more than it holds; a device offering more keeps its fastest rates. */
};

#ifdef __cplusplus
}
#endif

#endif /* PWF_TYPES_H */
