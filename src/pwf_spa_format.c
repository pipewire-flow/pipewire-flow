/* SPDX-License-Identifier: MIT */

#include <string.h>

#include <spa/param/audio/dsp-utils.h>
#include <spa/param/param.h>
#include <spa/param/buffers.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>

#include "pwf_spa_format_internal.h"

static const struct {
    const char* name;
    enum spa_audio_format format;
} pwf_audio_sample_formats[] = {
    { "U8",  SPA_AUDIO_FORMAT_U8 },
    { "S16", SPA_AUDIO_FORMAT_S16 },
    { "S24", SPA_AUDIO_FORMAT_S24 },
    { "S24_32", SPA_AUDIO_FORMAT_S24_32 },
    { "S32", SPA_AUDIO_FORMAT_S32 },
    { "F32", SPA_AUDIO_FORMAT_F32 },
};

enum spa_audio_format pwf_spa_lookup_audio_format(const char* name)
{
    for (size_t i = 0; i < SPA_N_ELEMENTS(pwf_audio_sample_formats); i++) {
        if (strcmp(pwf_audio_sample_formats[i].name, name) == 0)
            return pwf_audio_sample_formats[i].format;
    }
    return SPA_AUDIO_FORMAT_UNKNOWN;
}

const struct spa_pod* pwf_spa_build_audio_format(struct spa_pod_builder* b, const struct pwf_audio_config* config,
                                                  enum spa_audio_format fmt)
{
    struct spa_audio_info_raw info = {
        .format = fmt,
        .rate = (uint32_t)config->sample_rate,
        .channels = (uint32_t)config->channels,
    };
    return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

static const struct {
    const char* name;
    enum spa_video_format format;
} pwf_pixel_formats[] = {
    { "RGB", SPA_VIDEO_FORMAT_RGB },
    { "YUYV", SPA_VIDEO_FORMAT_YUY2 },
    { "NV12", SPA_VIDEO_FORMAT_NV12 },
    { "NV21", SPA_VIDEO_FORMAT_NV21 },
    { "I420", SPA_VIDEO_FORMAT_I420 },
};

enum spa_video_format pwf_spa_lookup_pixel_format(const char* name)
{
    for (size_t i = 0; i < SPA_N_ELEMENTS(pwf_pixel_formats); i++) {
        if (strcmp(pwf_pixel_formats[i].name, name) == 0)
            return pwf_pixel_formats[i].format;
    }
    return SPA_VIDEO_FORMAT_UNKNOWN;
}

const char* pwf_spa_pixel_format_name_or_null(enum spa_video_format format)
{
    /* MJPEG is the only encoded format this library builds. */
    if (format == SPA_VIDEO_FORMAT_ENCODED)
        return "MJPG";
    for (size_t i = 0; i < SPA_N_ELEMENTS(pwf_pixel_formats); i++) {
        if (pwf_pixel_formats[i].format == format)
            return pwf_pixel_formats[i].name;
    }
    return NULL;
}

const char* pwf_spa_pixel_format_name(enum spa_video_format format)
{
    const char* name = pwf_spa_pixel_format_name_or_null(format);
    return name ? name : "unknown";
}

const struct spa_pod* pwf_spa_build_video_format(struct spa_pod_builder* b, const struct pwf_video_config* config,
                                                  enum spa_video_format fmt)
{
    struct spa_rectangle size = SPA_RECTANGLE((uint32_t)config->width, (uint32_t)config->height);

    struct spa_pod_frame f;
    spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(fmt),
        SPA_FORMAT_VIDEO_size,   SPA_POD_Rectangle(&size), 0);
    if (config->fps > 0) {
        /* Fixed frame rate requested by the caller. */
        struct spa_fraction fr = SPA_FRACTION((uint32_t)config->fps, 1);
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&fr), 0);
    } else {
        /* fps == 0: let the source negotiate the frame rate. */
        struct spa_fraction fr_def = SPA_FRACTION(30, 1);
        struct spa_fraction fr_min = SPA_FRACTION(0, 1);
        struct spa_fraction fr_max = SPA_FRACTION(1000, 1);
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate,
            SPA_POD_CHOICE_RANGE_Fraction(&fr_def, &fr_min, &fr_max), 0);
    }
    return spa_pod_builder_pop(b, &f);
}

bool pwf_spa_pixel_format_is_mjpg(const char* name)
{
    return strcmp(name, "MJPG") == 0;
}

bool pwf_spa_pixel_format_is_h264(const char* name)
{
    return strcmp(name, "H264") == 0;
}

const struct spa_pod* pwf_spa_build_video_format_mjpg(struct spa_pod_builder* b, const struct pwf_video_config* config)
{
    /* MJPEG has no raw pixel-layout enum, so it gets its own POD shape
     * instead of going through pwf_spa_build_video_format(). */
    struct spa_video_info_mjpg info = {
        .size = SPA_RECTANGLE((uint32_t)config->width, (uint32_t)config->height),
    };
    if (config->fps > 0)
        info.framerate = SPA_FRACTION((uint32_t)config->fps, 1);
    return spa_format_video_mjpg_build(b, SPA_PARAM_EnumFormat, &info);
}

const struct spa_pod* pwf_spa_build_video_format_h264(struct spa_pod_builder* b, const struct pwf_video_config* config)
{
    struct spa_video_info_h264 info = {
        .size = SPA_RECTANGLE((uint32_t)config->width, (uint32_t)config->height),
    };
    if (config->fps > 0)
        info.framerate = SPA_FRACTION((uint32_t)config->fps, 1);
    return spa_format_video_h264_build(b, SPA_PARAM_EnumFormat, &info);
}

const struct spa_pod* pwf_spa_build_signal_format(struct spa_pod_builder* b)
{
    struct spa_audio_info_dsp info = { .format = SPA_AUDIO_FORMAT_DSP_F32 };
    return spa_format_audio_dsp_build(b, SPA_PARAM_EnumFormat, &info);
}

const struct spa_pod* pwf_spa_build_event_format(struct spa_pod_builder* b)
{
    struct spa_pod_frame f;
    spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_application),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_control), 0);
    return spa_pod_builder_pop(b, &f);
}

const struct spa_pod* pwf_spa_build_dmabuf_buffers(struct spa_pod_builder* b, unsigned int extra_buffers)
{
    /* Advertise DmaBuf only: a source that cannot provide it fails to
     * negotiate rather than falling back to a CPU copy. Only dataType and
     * the pool count are constrained; block size/stride stay negotiable. */
    int min_buffers = 2 + (int)extra_buffers;
    return spa_pod_builder_add_object(b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers,  SPA_POD_CHOICE_RANGE_Int(min_buffers, min_buffers, 32),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(1 << SPA_DATA_DmaBuf));
}

const struct spa_pod* pwf_spa_build_cpu_buffers(struct spa_pod_builder* b)
{
    /* Excludes DmaBuf, which a source could otherwise offer alongside
     * MemPtr even though nothing here opted into DMABUF import. */
    return spa_pod_builder_add_object(b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_dataType, SPA_POD_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)));
}

const struct spa_pod* pwf_spa_build_meta_header(struct spa_pod_builder* b)
{
    return spa_pod_builder_add_object(b,
        SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
        SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header)));
}
