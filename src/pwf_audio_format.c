/* SPDX-License-Identifier: MIT */

#include "pwf_spa_format_internal.h"
#include "pwf_stream_internal.h"

size_t pwf_audio_bytes_per_frame(enum spa_audio_format format, int channels)
{
    if (channels <= 0)
        return 0;

    size_t sample;
    switch (format) {
    case SPA_AUDIO_FORMAT_U8:  sample = 1; break;
    case SPA_AUDIO_FORMAT_S16: sample = 2; break;
    case SPA_AUDIO_FORMAT_S24: sample = 3; break; /* packed, not padded to 4 */
    case SPA_AUDIO_FORMAT_S24_32:
    case SPA_AUDIO_FORMAT_S32:
    case SPA_AUDIO_FORMAT_F32: sample = 4; break;
    default: return 0;
    }
    return sample * (size_t)channels;
}

int pwf_stream_set_audio_config(pwf_stream_h handle, const pwf_audio_config* config)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream || stream->type != PWF_DATA_AUDIO || !config)
        return PWF_ERR_INVALID_ARG;
    if (pwf_stream_refuse_in_callback(stream, true, __func__))
        return PWF_ERR_IN_CALLBACK;
    if (config->sample_rate <= 0 || config->channels <= 0)
        return PWF_ERR_INVALID_FORMAT;

    enum spa_audio_format fmt = pwf_spa_lookup_audio_format(config->format ? config->format : "S16");
    if (fmt == SPA_AUDIO_FORMAT_UNKNOWN)
        return PWF_ERR_INVALID_FORMAT;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[3];
    params[0] = pwf_spa_build_audio_format(&b, config, fmt);
    params[1] = pwf_spa_build_meta_header(&b);
    params[2] = pwf_spa_build_cpu_buffers(&b);

    int res = pwf_stream_internal_connect(stream, params, 3, false);
    if (res < 0)
        return res;

    stream->format.audio.sample_rate = config->sample_rate;
    stream->format.audio.channels = config->channels;
    stream->format.audio.format = fmt;
    stream->bytes_per_frame = pwf_audio_bytes_per_frame(fmt, config->channels);
    stream->format_set = true;
    stream->state = PWF_STREAM_STATE_FORMAT_SET;
    return PWF_OK;
}
