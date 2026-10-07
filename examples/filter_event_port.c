/* SPDX-License-Identifier: MIT */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "pwf/pwf_filter.h"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static const char* kind_name(enum pwf_event_kind kind)
{
    switch (kind) {
    case PWF_EVENT_MIDI:
        return "midi";
    case PWF_EVENT_OSC:
        return "osc";
    case PWF_EVENT_PROPERTY:
        return "property";
    default:
        return "unknown";
    }
}

/* Echoes every event received on the input event port back out through
 * the output event port, one process cycle at a time. Never touches a
 * spa_pod or any other SPA/PipeWire type directly. */
static void on_process(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                       void* user_data)
{
    (void)filter;
    (void)user_data;
    if (n_buffers < 2)
        return;

    struct pwf_filter_port* in = buffers[0].port;
    struct pwf_filter_port* out = buffers[1].port;

    size_t count = pwf_filter_port_get_event_count(in);
    for (size_t i = 0; i < count; i++) {
        struct pwf_event ev;
        if (pwf_filter_port_get_event(in, i, &ev) != PWF_OK)
            continue;
        printf("filter_event_port: kind=%s offset=%u size=%zu\n", kind_name(ev.kind), ev.offset, ev.size);
        pwf_filter_port_push_event(out, &ev);
    }
}

int main(void)
{
    signal(SIGINT, on_signal);

    struct pwf_filter* filter = pwf_filter_create("pwf-filter-event-port", on_process, NULL);
    if (!filter) {
        fprintf(stderr, "failed to create filter (is PipeWire running?)\n");
        return 1;
    }

    struct pwf_filter_port* in = pwf_filter_add_event_port(filter, PWF_FILTER_PORT_INPUT);
    struct pwf_filter_port* out = pwf_filter_add_event_port(filter, PWF_FILTER_PORT_OUTPUT);
    if (!in || !out) {
        fprintf(stderr, "failed to add filter ports\n");
        pwf_filter_destroy(filter);
        return 1;
    }

    printf("in port kind=%d, out port kind=%d (PWF_DATA_EVENT=%d)\n", pwf_filter_port_get_type(in),
           pwf_filter_port_get_type(out), PWF_DATA_EVENT);

    if (pwf_filter_start(filter) != PWF_OK) {
        fprintf(stderr, "failed to start filter\n");
        pwf_filter_destroy(filter);
        return 1;
    }

    printf("echoing events from the input port to the output port, press Ctrl+C to stop...\n");
    while (g_running)
        sleep(1);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);
    return 0;
}
