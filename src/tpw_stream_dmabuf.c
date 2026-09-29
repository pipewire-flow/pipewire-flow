/* SPDX-License-Identifier: MIT */

#include <spa/pod/builder.h>

#include "tpw_dmabuf_internal.h"
#include "tpw_log_internal.h"
#include "tpw_spa_format_internal.h"
#include "tpw_stream_internal.h"
#include "tpw_pw_dl.h"

void tpw_stream_dmabuf_update_params(struct tpw_stream* stream)
{
    if (!stream || !stream->use_dmabuf)
        return;

    uint8_t buffer[512];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    params[0] = tpw_spa_build_dmabuf_buffers(&b, 0);

    pw_stream_update_params(stream->pw_stream, params, 1);
}

void tpw_stream_dmabuf_log_unavailable(struct tpw_stream* stream)
{
    if (!stream || !stream->use_dmabuf)
        return;
    tpw_log_warning("stream: could not negotiate DMABUF with its source; "
                    "the stream will deliver no frames");
}

size_t tpw_stream_get_dmabuf_planes(tpw_stream_h handle, tpw_dmabuf_plane* planes, size_t planes_len)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream || !stream->current_dmabuf_buf)
        return 0;

    stream->dmabuf_retrieved = true;
    return tpw_dmabuf_extract_planes(stream->current_dmabuf_buf, planes, planes_len);
}
