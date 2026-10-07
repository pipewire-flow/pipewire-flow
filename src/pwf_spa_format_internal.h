/* SPDX-License-Identifier: MIT */

#ifndef PWF_SPA_FORMAT_INTERNAL_H
#define PWF_SPA_FORMAT_INTERNAL_H

#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>

#include "pwf/pwf_stream.h"

/* Maps a sample format string (e.g. "S16") to its enum spa_audio_format,
 * or SPA_AUDIO_FORMAT_UNKNOWN if unrecognized. */
enum spa_audio_format pwf_spa_lookup_audio_format(const char* name);

/* Builds the raw-audio SPA_TYPE_OBJECT_Format POD for `config`/`fmt`
 * using `b`. Shared by pwf_stream's and pwf_filter's audio format
 * setters so the SPA POD-building code exists only once. Does not
 * validate `config`. */
const struct spa_pod* pwf_spa_build_audio_format(struct spa_pod_builder* b, const pwf_audio_config* config,
                                                  enum spa_audio_format fmt);

/* Maps a pixel_format string (e.g. "RGB") to its enum spa_video_format,
 * or SPA_VIDEO_FORMAT_UNKNOWN if unrecognized. */
enum spa_video_format pwf_spa_lookup_pixel_format(const char* name);

/* Names a pixel format for a log message, the reverse of
 * pwf_spa_lookup_pixel_format(); "unknown" if unrecognized. */
const char* pwf_spa_pixel_format_name(enum spa_video_format format);

/* Like pwf_spa_pixel_format_name(), but NULL rather than "unknown", so a
 * caller enumerating a device can skip what it could not ask for anyway. */
const char* pwf_spa_pixel_format_name_or_null(enum spa_video_format format);

/* Builds the raw-video SPA_TYPE_OBJECT_Format POD for `config`/`fmt`
 * using `b`. Shared by pwf_stream's and pwf_filter's video format
 * setters. Does not validate `config`. */
const struct spa_pod* pwf_spa_build_video_format(struct spa_pod_builder* b, const pwf_video_config* config,
                                                  enum spa_video_format fmt);

/* True if `name` selects MJPEG, which is not a raw pixel layout and so
 * cannot go through pwf_spa_lookup_pixel_format(). */
bool pwf_spa_pixel_format_is_mjpg(const char* name);

/* True if `name` selects H.264 encoded format. */
bool pwf_spa_pixel_format_is_h264(const char* name);

/* Builds the MJPEG SPA_TYPE_OBJECT_Format POD for `config` using `b`.
 * Shared by pwf_stream's and pwf_filter's video format setters. */
const struct spa_pod* pwf_spa_build_video_format_mjpg(struct spa_pod_builder* b, const pwf_video_config* config);

/* Builds the H.264 SPA_TYPE_OBJECT_Format POD for `config` using `b`.
 * Shared by pwf_stream's and pwf_filter's video format setters. */
const struct spa_pod* pwf_spa_build_video_format_h264(struct spa_pod_builder* b, const pwf_video_config* config);

/* Builds the SPA_TYPE_OBJECT_Format POD for a filter signal port using
 * `b`: audio/dsp media type fixed to 32-bit float, no per-instance
 * fields since there is nothing left for a caller to configure. */
const struct spa_pod* pwf_spa_build_signal_format(struct spa_pod_builder* b);

/* Builds the SPA_TYPE_OBJECT_Format POD for a filter event port using
 * `b`: application/control media type, no per-instance fields. */
const struct spa_pod* pwf_spa_build_event_format(struct spa_pod_builder* b);

/* Builds a SPA_PARAM_Buffers POD advertising DmaBuf as the only accepted
 * memory type (dataType = 1<<SPA_DATA_DmaBuf). `extra_buffers` is added to
 * the requested pool count so a held port can retain one frame while the
 * next is produced. Applied via pw_filter_update_params() in param_changed
 * (never at add time — crashes PipeWire 1.0.5). */
const struct spa_pod* pwf_spa_build_dmabuf_buffers(struct spa_pod_builder* b, unsigned int extra_buffers);

/* Builds a SPA_PARAM_Buffers POD restricting dataType to CPU-mappable
 * memory (MemPtr|MemFd), excluding DmaBuf, for a non-DMABUF port/stream. */
const struct spa_pod* pwf_spa_build_cpu_buffers(struct spa_pod_builder* b);

/* Requests SPA_META_Header on negotiated buffers, so a source that can
 * attach a capture timestamp does so. Without this the pts in
 * pwf_stream_buffer/pwf_filter_port_buffer is always -1. */
const struct spa_pod* pwf_spa_build_meta_header(struct spa_pod_builder* b);

#endif /* PWF_SPA_FORMAT_INTERNAL_H */
