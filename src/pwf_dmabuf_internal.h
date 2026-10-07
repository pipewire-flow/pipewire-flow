/* SPDX-License-Identifier: MIT */

#ifndef PWF_DMABUF_INTERNAL_H
#define PWF_DMABUF_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <spa/buffer/buffer.h>

#include "pwf/pwf_stream.h"

/* Whether `b`'s first plane is DMABUF-backed. This is the "buffer
 * present" test for a DMABUF port or stream, whose data pointer is
 * always NULL. Shared by pwf_filter and pwf_stream. */
bool pwf_dmabuf_buffer_present(struct spa_buffer* b);

/* Fills up to `planes_len` entries of `planes` with every DMABUF-typed
 * plane of `b` and returns the plane count. Shared by pwf_filter and
 * pwf_stream. */
size_t pwf_dmabuf_extract_planes(struct spa_buffer* b, pwf_dmabuf_plane* planes, size_t planes_len);

#endif /* PWF_DMABUF_INTERNAL_H */
