/* SPDX-License-Identifier: MIT */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pwf/pwf_filter.h"
#include "pwf_filter_internal.h"
#include "pwf_test.h"

static int g_cycles = 0;
static size_t g_last_event_count = 0;
static uint32_t g_last_offset = 0;
static pwf_event_kind g_last_kind = PWF_EVENT_UNKNOWN;
static uint8_t g_last_data[64];
static size_t g_last_size = 0;

static int g_output_push_calls = 0;
static int g_output_push_unexpected = 0;

static void process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    (void)user_data;
    g_cycles++;
    if (n_buffers < 1)
        return;

    /* Port 1 (added second) is the output event port. Unlinked, it
     * never gets a real dequeue-able buffer (confirmed in CI), so only
     * {OK, INVALID_ARG} are valid outcomes here — never anything else. */
    if (n_buffers >= 2) {
        uint8_t note_on[3] = { 0x90, 0x3c, 0x64 };
        pwf_event out_ev = { .offset = 0, .kind = PWF_EVENT_MIDI, .key = NULL, .data = note_on,
                              .size = sizeof(note_on) };
        g_output_push_calls++;
        int res = pwf_filter_port_push_event(buffers[1].port, &out_ev);
        if (res != PWF_OK && res != PWF_ERR_INVALID_ARG)
            g_output_push_unexpected++;
    }

    pwf_filter_port_h in = buffers[0].port;
    size_t count = pwf_filter_port_get_event_count(in);
    if (count == 0)
        return;
    /* Only overwrite the "last observed" globals on a cycle that
     * actually had an event — the many empty cycles before/after must
     * not erase what a prior cycle already recorded. */
    g_last_event_count = count;

    pwf_event ev;
    if (pwf_filter_port_get_event(in, 0, &ev) != PWF_OK)
        return;
    g_last_offset = ev.offset;
    g_last_kind = ev.kind;
    g_last_size = ev.size < sizeof(g_last_data) ? ev.size : sizeof(g_last_data);
    if (ev.data && g_last_size > 0)
        memcpy(g_last_data, ev.data, g_last_size);
}

/* Whitebox helper: stages one pending event on a scratch port, the way
 * pwf_filter_port_push_event() would, without needing a real filter
 * (pwf_filter_event_finish_output()/pwf_filter_event_decode() touch
 * only the port struct's own arrays, never port->filter). */
static void stage_pending(struct pwf_filter_port* port, pwf_event_kind kind, const char* key, const void* data,
                           size_t size, uint32_t offset)
{
    struct pwf_filter_pending_event* grown =
        realloc(port->pending_events, (port->n_pending_events + 1) * sizeof(*grown));
    port->pending_events = grown;
    struct pwf_filter_pending_event* dst = &port->pending_events[port->n_pending_events];
    dst->offset = offset;
    dst->kind = kind;
    dst->key = key;
    dst->size = size;
    dst->data = NULL;
    if (size > 0) {
        dst->data = malloc(size);
        memcpy(dst->data, data, size);
    }
    port->n_pending_events++;
}

int main(void)
{
    /* --- Whitebox round-trip: encode a control sequence from staged
     * events, then decode it back, and confirm every field survives. --- */
    struct pwf_filter_port scratch_out = { .media_type = PWF_DATA_EVENT, .direction = PWF_FILTER_PORT_OUTPUT };

    uint8_t midi_bytes[3] = { 0x90, 0x40, 0x7f };
    stage_pending(&scratch_out, PWF_EVENT_MIDI, NULL, midi_bytes, sizeof(midi_bytes), 10);

    const char osc_bytes[] = "/synth/freq";
    stage_pending(&scratch_out, PWF_EVENT_OSC, NULL, osc_bytes, sizeof(osc_bytes), 20);

    float vol = 0.75f;
    stage_pending(&scratch_out, PWF_EVENT_PROPERTY, "volume", &vol, sizeof(vol), 30);

    uint8_t encode_buf[1024];
    size_t encoded = pwf_filter_event_finish_output(&scratch_out, encode_buf, sizeof(encode_buf));
    PWF_ASSERT(encoded > 0);
    PWF_ASSERT_EQ(scratch_out.n_pending_events, (size_t)0);
    free(scratch_out.pending_events);

    struct pwf_filter_port scratch_in = { .media_type = PWF_DATA_EVENT, .direction = PWF_FILTER_PORT_INPUT };
    pwf_filter_event_decode(&scratch_in, encode_buf, encoded);
    PWF_ASSERT_EQ(scratch_in.n_incoming_events, (size_t)3);

    PWF_ASSERT_EQ(scratch_in.incoming_events[0].offset, (uint32_t)10);
    PWF_ASSERT_EQ(scratch_in.incoming_events[0].kind, PWF_EVENT_MIDI);
    PWF_ASSERT(scratch_in.incoming_events[0].key == NULL);
    PWF_ASSERT_EQ(scratch_in.incoming_events[0].size, sizeof(midi_bytes));
    PWF_ASSERT(memcmp(scratch_in.incoming_events[0].data, midi_bytes, sizeof(midi_bytes)) == 0);

    PWF_ASSERT_EQ(scratch_in.incoming_events[1].offset, (uint32_t)20);
    PWF_ASSERT_EQ(scratch_in.incoming_events[1].kind, PWF_EVENT_OSC);
    PWF_ASSERT_EQ(scratch_in.incoming_events[1].size, sizeof(osc_bytes));
    PWF_ASSERT(memcmp(scratch_in.incoming_events[1].data, osc_bytes, sizeof(osc_bytes)) == 0);

    PWF_ASSERT_EQ(scratch_in.incoming_events[2].offset, (uint32_t)30);
    PWF_ASSERT_EQ(scratch_in.incoming_events[2].kind, PWF_EVENT_PROPERTY);
    PWF_ASSERT(strcmp(scratch_in.incoming_events[2].key, "volume") == 0);
    PWF_ASSERT_EQ(scratch_in.incoming_events[2].size, sizeof(vol));
    float decoded_vol;
    memcpy(&decoded_vol, scratch_in.incoming_events[2].data, sizeof(decoded_vol));
    PWF_ASSERT_EQ(decoded_vol, vol);
    free(scratch_in.incoming_events);

    /* --- Zero-event decode is not an error. --- */
    struct pwf_filter_port scratch_empty = { .media_type = PWF_DATA_EVENT, .direction = PWF_FILTER_PORT_INPUT };
    pwf_filter_event_decode(&scratch_empty, NULL, 0);
    PWF_ASSERT_EQ(scratch_empty.n_incoming_events, (size_t)0);

    /* --- Public API: add ports, reject bad pushes, real cycle round
     * trip through the input-staging path. --- */
    pwf_filter_h filter = pwf_filter_create("pwf-test-event", process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    pwf_filter_port_h ev_in = pwf_filter_add_event_port(filter, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(ev_in != NULL);
    pwf_filter_port_h ev_out = pwf_filter_add_event_port(filter, PWF_FILTER_PORT_OUTPUT);
    PWF_ASSERT(ev_out != NULL);
    pwf_filter_port_h audio_in = pwf_filter_add_audio_port(
        filter, PWF_FILTER_PORT_INPUT, &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 });
    PWF_ASSERT(audio_in != NULL);

    /* get_event()/event_count() reject a non-event port, an output
     * event port (get_event() is input-only), and an out-of-range
     * index — none of this needs a real cycle to have run yet. */
    pwf_event scratch_ev;
    PWF_ASSERT_EQ(pwf_filter_port_get_event(audio_in, 0, &scratch_ev), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_get_event(ev_out, 0, &scratch_ev), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_get_event(ev_in, 0, &scratch_ev), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_get_event_count(audio_in), (size_t)0);
    PWF_ASSERT_EQ(pwf_filter_port_get_event_count(ev_out), (size_t)0);

    uint8_t junk[4] = { 0 };
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, ev_in, junk, sizeof(junk), -1), PWF_ERR_INVALID_ARG);

    pwf_event bad_key_on_midi = { .offset = 0, .kind = PWF_EVENT_MIDI, .key = "volume", .data = NULL, .size = 0 };
    PWF_ASSERT_EQ(pwf_filter_port_push_event(ev_in, &bad_key_on_midi), PWF_ERR_INVALID_ARG);

    int one = 1;
    pwf_event unrecognized_key = { .offset = 0, .kind = PWF_EVENT_PROPERTY, .key = "not-a-real-property",
                                    .data = &one, .size = sizeof(one) };
    PWF_ASSERT_EQ(pwf_filter_port_push_event(ev_in, &unrecognized_key), PWF_ERR_INVALID_ARG);

    pwf_event unknown_kind = { .offset = 0, .kind = PWF_EVENT_UNKNOWN, .key = NULL, .data = NULL, .size = 0 };
    PWF_ASSERT_EQ(pwf_filter_port_push_event(ev_in, &unknown_kind), PWF_ERR_INVALID_ARG);

    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);
    sleep(1);
    PWF_ASSERT_EQ(g_last_event_count, (size_t)0);

    uint8_t midi2[3] = { 0x80, 0x3c, 0x00 };
    pwf_event push_ev = { .offset = 5, .kind = PWF_EVENT_MIDI, .key = NULL, .data = midi2, .size = sizeof(midi2) };
    PWF_ASSERT_EQ(pwf_filter_port_push_event(ev_in, &push_ev), PWF_OK);
    sleep(1);

    PWF_ASSERT_EQ(g_last_event_count, (size_t)1);
    PWF_ASSERT_EQ(g_last_offset, (uint32_t)5);
    PWF_ASSERT_EQ(g_last_kind, PWF_EVENT_MIDI);
    PWF_ASSERT_EQ(g_last_size, sizeof(midi2));
    PWF_ASSERT(memcmp(g_last_data, midi2, sizeof(midi2)) == 0);

    PWF_ASSERT(g_cycles > 0);

    /* Can't reach the success path without a real link (see process_cb);
     * this only guards that the call stays within its documented contract. */
    PWF_ASSERT(g_output_push_calls > 0);
    PWF_ASSERT_EQ(g_output_push_unexpected, 0);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);

    /* Adding an event port after the filter has started is rejected. */
    pwf_filter_h started = pwf_filter_create("pwf-test-event-started", process_cb, NULL);
    PWF_ASSERT(started != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(started, PWF_FILTER_PORT_INPUT,
                                          &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 }) != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(started), PWF_OK);
    PWF_ASSERT(pwf_filter_add_event_port(started, PWF_FILTER_PORT_OUTPUT) == NULL);
    pwf_filter_stop(started, false);
    pwf_filter_destroy(started);

    return 0;
}
