/* SPDX-License-Identifier: MIT */

#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>

#include "pwf_dmabuf_internal.h"
#include "pwf_log_internal.h"
#include "pwf_stream_internal.h"

/* Hands the current DMABUF-backed buffer to the callback with no CPU
 * data, then warns once per interval if the callback never read its
 * descriptor through pwf_stream_get_dmabuf_planes(). */
static void pwf_stream_deliver_dmabuf(struct pwf_stream* stream, struct spa_buffer* buf, int64_t pts)
{
    stream->current_dmabuf_buf = buf;
    stream->dmabuf_retrieved = false;

    if (stream->data_cb) {
        struct pwf_stream_buffer sbuf = { .data = NULL, .size = 0, .pts = pts };
        stream->data_cb(stream, &sbuf, stream->user_data);
    }

    if (!stream->dmabuf_retrieved &&
        pwf_rate_limited(&stream->dmabuf_unretrieved_last_log_ns, &stream->dmabuf_unretrieved_suppressed,
                          pwf_monotonic_ns())) {
        pwf_log_warning("stream: a DMABUF frame's descriptor was never read via "
                        "pwf_stream_get_dmabuf_planes() (%llu more suppressed)",
                        (unsigned long long)stream->dmabuf_unretrieved_suppressed);
        stream->dmabuf_unretrieved_suppressed = 0;
    }

    stream->current_dmabuf_buf = NULL;
}

/* Zero-copy handoff: dequeue, hand the mapped pointer/size (or, for a
 * DMABUF stream, the fd via the accessor) to the callback, then queue
 * back once the callback returns. */
void pwf_stream_on_process(void* data)
{
    struct pwf_stream* stream = data;
    struct pw_buffer* b = pw_stream_dequeue_buffer(stream->pw_stream);
    if (!b)
        return;

    struct spa_buffer* buf = b->buffer;

    /* SPA_META_Header carries the source's capture clock (ALSA/V4L2,
     * etc.); not every source attaches it, so -1 signals "unavailable"
     * rather than guessing a timestamp. */
    struct spa_meta_header* h = spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(*h));
    int64_t pts = h ? h->pts : -1;

    pwf_stream_processing = stream;
    if (stream->use_dmabuf) {
        if (pwf_dmabuf_buffer_present(buf))
            pwf_stream_deliver_dmabuf(stream, buf, pts);
    } else {
        struct spa_data* d = &buf->datas[0];
        if (d->data && d->chunk && d->chunk->size > 0 && stream->data_cb) {
            struct pwf_stream_buffer sbuf = { .data = d->data, .size = d->chunk->size, .pts = pts };
            stream->data_cb(stream, &sbuf, stream->user_data);
        }
    }
    pwf_stream_processing = NULL;

    pw_stream_queue_buffer(stream->pw_stream, b);
}
