/* SPDX-License-Identifier: MIT */

#include <stdlib.h>
#include <string.h>

#include <spa/control/control.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>

#include "pwf_filter_internal.h"

struct pwf_property_entry {
    const char* name;
    uint32_t spa_prop_id;
};

/* A starter vocabulary of well-known SPA properties, translated to/from
 * their real enum spa_prop id so property events stay wire-compatible
 * with other PipeWire clients. Grows without changing the public API
 * shape (pwf_event.key is always a plain string). */
static const struct pwf_property_entry pwf_property_table[] = {
    { "volume", SPA_PROP_volume },
    { "mute", SPA_PROP_mute },
    { "channelVolumes", SPA_PROP_channelVolumes },
};

static const struct pwf_property_entry* pwf_property_find_by_name(const char* name)
{
    if (!name)
        return NULL;
    for (size_t i = 0; i < SPA_N_ELEMENTS(pwf_property_table); i++) {
        if (strcmp(pwf_property_table[i].name, name) == 0)
            return &pwf_property_table[i];
    }
    return NULL;
}

static const struct pwf_property_entry* pwf_property_find_by_id(uint32_t id)
{
    for (size_t i = 0; i < SPA_N_ELEMENTS(pwf_property_table); i++) {
        if (pwf_property_table[i].spa_prop_id == id)
            return &pwf_property_table[i];
    }
    return NULL;
}

static bool pwf_event_kind_key_valid(enum pwf_event_kind kind, const char* key)
{
    if (kind == PWF_EVENT_PROPERTY)
        return pwf_property_find_by_name(key) != NULL;
    if (kind == PWF_EVENT_MIDI || kind == PWF_EVENT_OSC)
        return key == NULL;
    return false; /* PWF_EVENT_UNKNOWN (or any other value) can't be pushed */
}

static bool pwf_filter_incoming_event_append(struct pwf_filter_port* port, const struct pwf_event* event)
{
    if (port->n_incoming_events == port->incoming_events_capacity) {
        size_t new_cap = port->incoming_events_capacity == 0 ? 4 : port->incoming_events_capacity * 2;
        struct pwf_event* grown = realloc(port->incoming_events, new_cap * sizeof(*grown));
        if (!grown)
            return false;
        port->incoming_events = grown;
        port->incoming_events_capacity = new_cap;
    }
    port->incoming_events[port->n_incoming_events++] = *event;
    return true;
}

void pwf_filter_event_decode(struct pwf_filter_port* port, const void* data, size_t size)
{
    port->n_incoming_events = 0;
    if (!data || size < sizeof(struct spa_pod))
        return;

    struct spa_pod* pod = spa_pod_from_data((void*)data, size, 0, size);
    if (!pod || !spa_pod_is_sequence(pod))
        return;

    struct spa_pod_sequence* seq = (struct spa_pod_sequence*)pod;
    struct spa_pod_control* c;
    SPA_POD_SEQUENCE_FOREACH(seq, c) {
        const struct spa_pod* value = &c->value;

        if (c->type == SPA_CONTROL_Midi || c->type == SPA_CONTROL_OSC) {
            const void* bytes = NULL;
            uint32_t len = 0;
            if (spa_pod_get_bytes(value, &bytes, &len) < 0)
                continue;
            struct pwf_event ev = {
                .offset = c->offset,
                .kind = (c->type == SPA_CONTROL_Midi) ? PWF_EVENT_MIDI : PWF_EVENT_OSC,
                .key = NULL,
                .data = bytes,
                .size = len,
            };
            pwf_filter_incoming_event_append(port, &ev);
        } else if (c->type == SPA_CONTROL_Properties) {
            if (!spa_pod_is_object_type(value, SPA_TYPE_OBJECT_Props))
                continue;
            const struct spa_pod_object* obj = (const struct spa_pod_object*)value;
            struct spa_pod_prop* p;
            SPA_POD_OBJECT_FOREACH(obj, p) {
                const struct pwf_property_entry* entry = pwf_property_find_by_id(p->key);
                if (!entry)
                    continue; /* property key outside our vocabulary: skipped */
                struct pwf_event ev = {
                    .offset = c->offset,
                    .kind = PWF_EVENT_PROPERTY,
                    .key = entry->name,
                    .data = SPA_POD_BODY(&p->value),
                    .size = SPA_POD_BODY_SIZE(&p->value),
                };
                pwf_filter_incoming_event_append(port, &ev);
            }
        } else {
            struct pwf_event ev = {
                .offset = c->offset,
                .kind = PWF_EVENT_UNKNOWN,
                .key = NULL,
                .data = SPA_POD_BODY(value),
                .size = SPA_POD_BODY_SIZE(value),
            };
            pwf_filter_incoming_event_append(port, &ev);
        }
    }
}

void pwf_filter_event_take_pending(struct pwf_filter_port* port)
{
    struct pwf_filter_pending_event* events = port->delivering_events;
    size_t capacity = port->delivering_events_capacity;

    port->delivering_events = port->pending_events;
    port->n_delivering_events = port->n_pending_events;
    port->delivering_events_capacity = port->pending_events_capacity;

    port->pending_events = events;
    port->n_pending_events = 0;
    port->pending_events_capacity = capacity;
}

void pwf_filter_event_load_delivering_as_incoming(struct pwf_filter_port* port)
{
    port->n_incoming_events = 0;
    for (size_t i = 0; i < port->n_delivering_events; i++) {
        struct pwf_filter_pending_event* pe = &port->delivering_events[i];
        struct pwf_event ev = { pe->offset, pe->kind, pe->key, pe->data, pe->size };
        pwf_filter_incoming_event_append(port, &ev);
    }
}

static void pwf_filter_event_free_entries(struct pwf_filter_pending_event* events, size_t* n_events)
{
    for (size_t i = 0; i < *n_events; i++)
        free(events[i].data);
    *n_events = 0;
}

void pwf_filter_event_clear_pending(struct pwf_filter_port* port)
{
    pwf_filter_event_free_entries(port->pending_events, &port->n_pending_events);
}

void pwf_filter_event_clear_delivering(struct pwf_filter_port* port)
{
    pwf_filter_event_free_entries(port->delivering_events, &port->n_delivering_events);
}

static bool pwf_filter_pending_event_append(struct pwf_filter_port* port, const struct pwf_event* event)
{
    if (port->n_pending_events == port->pending_events_capacity) {
        size_t new_cap = port->pending_events_capacity == 0 ? 4 : port->pending_events_capacity * 2;
        struct pwf_filter_pending_event* grown = realloc(port->pending_events, new_cap * sizeof(*grown));
        if (!grown)
            return false;
        port->pending_events = grown;
        port->pending_events_capacity = new_cap;
    }

    struct pwf_filter_pending_event* dst = &port->pending_events[port->n_pending_events];
    dst->offset = event->offset;
    dst->kind = event->kind;
    dst->key = (event->kind == PWF_EVENT_PROPERTY) ? pwf_property_find_by_name(event->key)->name : NULL;
    dst->data = NULL;
    dst->size = event->size;
    if (event->size > 0) {
        dst->data = malloc(event->size);
        if (!dst->data)
            return false;
        memcpy(dst->data, event->data, event->size);
    }

    port->n_pending_events++;
    return true;
}

static void pwf_filter_pending_event_pop_last(struct pwf_filter_port* port)
{
    if (port->n_pending_events == 0)
        return;
    port->n_pending_events--;
    free(port->pending_events[port->n_pending_events].data);
}

/* Encodes pending_events as-is into buf/maxsize; buf == NULL / maxsize
 * == 0 is a valid "dry run" that reports the size that would be needed
 * without writing anything (the standard SPA POD sizing idiom). */
static size_t pwf_filter_event_encode(struct pwf_filter_port* port, void* buf, size_t maxsize)
{
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, (uint32_t)maxsize);
    struct spa_pod_frame f;
    spa_pod_builder_push_sequence(&b, &f, 0);

    for (size_t i = 0; i < port->n_pending_events; i++) {
        struct pwf_filter_pending_event* pe = &port->pending_events[i];

        if (pe->kind == PWF_EVENT_MIDI || pe->kind == PWF_EVENT_OSC) {
            spa_pod_builder_control(&b, pe->offset, (pe->kind == PWF_EVENT_MIDI) ? SPA_CONTROL_Midi : SPA_CONTROL_OSC);
            spa_pod_builder_bytes(&b, pe->data, (uint32_t)pe->size);
        } else if (pe->kind == PWF_EVENT_PROPERTY) {
            const struct pwf_property_entry* entry = pwf_property_find_by_name(pe->key);
            if (!entry)
                continue; /* validated at push time; defensive only */
            spa_pod_builder_control(&b, pe->offset, SPA_CONTROL_Properties);
            struct spa_pod_frame obj_f;
            spa_pod_builder_push_object(&b, &obj_f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
            spa_pod_builder_prop(&b, entry->spa_prop_id, 0);
            spa_pod_builder_bytes(&b, pe->data, (uint32_t)pe->size);
            spa_pod_builder_pop(&b, &obj_f);
        }
    }

    spa_pod_builder_pop(&b, &f);
    return b.state.offset;
}

size_t pwf_filter_event_finish_output(struct pwf_filter_port* port, void* buf, size_t maxsize)
{
    size_t encoded = pwf_filter_event_encode(port, buf, maxsize);
    pwf_filter_event_clear_pending(port);
    return encoded;
}

void pwf_filter_event_free_port(struct pwf_filter_port* port)
{
    if (!port || port->media_type != PWF_DATA_EVENT)
        return;
    pwf_filter_event_clear_pending(port);
    pwf_filter_event_clear_delivering(port);
    free(port->pending_events);
    free(port->delivering_events);
    free(port->incoming_events);
}

size_t pwf_filter_port_get_event_count(struct pwf_filter_port* port_handle)
{
    struct pwf_filter_port* port = (struct pwf_filter_port*)port_handle;
    if (!port || port->media_type != PWF_DATA_EVENT || port->direction != PWF_FILTER_PORT_INPUT)
        return 0;
    return port->n_incoming_events;
}

int pwf_filter_port_get_event(struct pwf_filter_port* port_handle, size_t index, struct pwf_event* out)
{
    struct pwf_filter_port* port = (struct pwf_filter_port*)port_handle;
    if (!port || port->media_type != PWF_DATA_EVENT || port->direction != PWF_FILTER_PORT_INPUT || !out)
        return PWF_ERR_INVALID_ARG;
    if (index >= port->n_incoming_events)
        return PWF_ERR_INVALID_ARG;

    *out = port->incoming_events[index];
    return PWF_OK;
}

int pwf_filter_port_push_event(struct pwf_filter_port* port_handle, const struct pwf_event* event)
{
    struct pwf_filter_port* port = (struct pwf_filter_port*)port_handle;
    if (!port || port->media_type != PWF_DATA_EVENT || !event)
        return PWF_ERR_INVALID_ARG;
    if (event->size > 0 && !event->data)
        return PWF_ERR_INVALID_ARG;
    if (!pwf_event_kind_key_valid(event->kind, event->key))
        return PWF_ERR_INVALID_ARG;

    struct pwf_filter* filter = port->filter;
    if (port->direction == PWF_FILTER_PORT_INPUT) {
        /* The cycle swaps what is staged here, so the push lock is enough. */
        pthread_mutex_lock(&filter->push_lock);
        bool staged = pwf_filter_pending_event_append(port, event);
        pthread_mutex_unlock(&filter->push_lock);
        return staged ? PWF_OK : PWF_ERR_NO_MEMORY;
    }

    /* An output port is pushed from inside the processing callback, which
     * already runs on the loop's own thread; locking there would deadlock. */
    bool lock = pwf_filter_processing != filter;
    if (lock)
        pw_thread_loop_lock(filter->conn.loop);

    if (!pwf_filter_pending_event_append(port, event)) {
        if (lock)
            pw_thread_loop_unlock(filter->conn.loop);
        return PWF_ERR_NO_MEMORY;
    }

    /* A push must fit the output buffer this cycle dequeued, so an oversized
     * one is rejected here rather than silently truncated at encode time. */
    size_t needed = pwf_filter_event_encode(port, NULL, 0);
    if (needed > port->event_output_capacity) {
        pwf_filter_pending_event_pop_last(port);
        if (lock)
            pw_thread_loop_unlock(filter->conn.loop);
        return PWF_ERR_INVALID_ARG;
    }

    if (lock)
        pw_thread_loop_unlock(filter->conn.loop);
    return PWF_OK;
}
