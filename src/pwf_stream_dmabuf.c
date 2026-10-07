/* SPDX-License-Identifier: MIT */

#include <spa/pod/builder.h>

#include "pwf_dmabuf_internal.h"
#include "pwf_log_internal.h"
#include "pwf_spa_format_internal.h"
#include "pwf_stream_internal.h"

void pwf_stream_dmabuf_update_params(struct pwf_stream* stream)
{
    if (!stream || !stream->use_dmabuf)
        return;

    uint8_t buffer[512];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    params[0] = pwf_spa_build_dmabuf_buffers(&b, 0);

    pw_stream_update_params(stream->pw_stream, params, 1);
}

void pwf_stream_dmabuf_log_unavailable(struct pwf_stream* stream)
{
    if (!stream || !stream->use_dmabuf)
        return;
    pwf_log_warning("stream: could not negotiate DMABUF with its source; "
                    "the stream will deliver no frames");
}

size_t pwf_stream_get_dmabuf_planes(pwf_stream_h handle, pwf_dmabuf_plane* planes, size_t planes_len)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    if (!stream || !stream->current_dmabuf_buf)
        return 0;

    stream->dmabuf_retrieved = true;
    return pwf_dmabuf_extract_planes(stream->current_dmabuf_buf, planes, planes_len);
}
