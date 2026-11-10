/* SPDX-License-Identifier: MIT */

#include "pwf_dmabuf_internal.h"

bool pwf_buffer_opts_valid(const struct pwf_buffer_opts* opts)
{
    if (!opts)
        return true;
    if (opts->memory != PWF_BUFFER_MEMORY_AUTO && opts->memory != PWF_BUFFER_MEMORY_DMABUF)
        return false;
    if (opts->flags != 0)
        return false;
    for (size_t i = 0; i < sizeof(opts->reserved) / sizeof(opts->reserved[0]); i++) {
        if (opts->reserved[i] != 0)
            return false;
    }
    return true;
}

bool pwf_dmabuf_buffer_present(struct spa_buffer* b)
{
    if (!b || b->n_datas == 0)
        return false;
    return b->datas[0].type == SPA_DATA_DmaBuf;
}

size_t pwf_dmabuf_extract_planes(struct spa_buffer* b, struct pwf_dmabuf_plane* planes, size_t planes_len)
{
    if (!b)
        return 0;

    size_t count = 0;
    for (uint32_t i = 0; i < b->n_datas; i++) {
        struct spa_data* d = &b->datas[i];
        if (d->type != SPA_DATA_DmaBuf)
            continue;
        if (planes && count < planes_len) {
            planes[count].fd = (int)d->fd;
            planes[count].offset = d->mapoffset;
            planes[count].stride = d->chunk ? (uint32_t)d->chunk->stride : 0;
            planes[count].size = d->chunk ? d->chunk->size : d->maxsize;
        }
        count++;
    }
    return count;
}
