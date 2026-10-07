/* SPDX-License-Identifier: MIT */

/* Pushes race the cycle on PipeWire's data thread, and a deliberately slow
 * callback widens that window so a lost or torn push shows up reliably. */

#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "pwf_filter_internal.h"
#include "pwf_test.h"

#define PUSHES 400
#define SLOW_CALLBACK_US 3000

/* Polls `counter` for up to five seconds and reports whether it got there. */
static bool wait_for(const atomic_uint* counter, unsigned target)
{
    for (int i = 0; i < 500; i++) {
        if (atomic_load(counter) >= target)
            return true;
        usleep(10000);
    }
    return false;
}

/* Every pushed event is delivered once, in push order. */

static atomic_uint g_events_seen;
static atomic_uint g_events_misordered;

static void event_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)filter;
    (void)n;
    (void)user_data;

    struct pwf_filter_port* in = buffers[0].port;
    size_t count = pwf_filter_port_get_event_count(in);
    for (size_t i = 0; i < count; i++) {
        struct pwf_event ev;
        uint32_t value = 0;
        if (pwf_filter_port_get_event(in, i, &ev) == PWF_OK && ev.size == sizeof(value))
            memcpy(&value, ev.data, sizeof(value));
        /* A lost event shifts every later value off its expected position. */
        if (value != atomic_load(&g_events_seen))
            atomic_fetch_add(&g_events_misordered, 1);
        atomic_fetch_add(&g_events_seen, 1);
    }
    usleep(SLOW_CALLBACK_US);
}

static void test_no_event_is_lost(void)
{
    struct pwf_filter* filter = pwf_filter_create("pwf-test-push-race-events", event_cb, NULL);
    PWF_ASSERT(filter != NULL);
    struct pwf_filter_port* in = pwf_filter_add_event_port(filter, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(in != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    for (uint32_t i = 0; i < PUSHES; i++) {
        struct pwf_event ev = { .offset = 0, .kind = PWF_EVENT_MIDI, .key = NULL, .data = &i, .size = sizeof(i) };
        PWF_ASSERT_EQ(pwf_filter_port_push_event(in, &ev), PWF_OK);
        usleep(300 + (i % 7) * 300); /* The spacing lands pushes across the whole cycle. */
    }

    bool all_seen = wait_for(&g_events_seen, PUSHES);
    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);

    unsigned seen = atomic_load(&g_events_seen);
    unsigned misordered = atomic_load(&g_events_misordered);
    PWF_ASSERT(all_seen);
    PWF_ASSERT_EQ(seen, (unsigned)PUSHES);
    PWF_ASSERT_EQ(misordered, 0u);
}

/* A delivered buffer does not change while the callback reads it. */

static atomic_uint g_data_fresh;
static atomic_uint g_data_torn;

/* Each push is filled with one byte value, and its size follows from it. */
static size_t size_for(uint8_t value)
{
    return 16 + (size_t)value * 8;
}

static void data_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)filter;
    (void)n;
    (void)user_data;

    const struct pwf_filter_port_buffer* b = &buffers[0];
    if (!b->fresh || !b->data || b->size == 0) {
        usleep(SLOW_CALLBACK_US);
        return;
    }

    const uint8_t* bytes = b->data;
    uint8_t value = bytes[0];
    usleep(SLOW_CALLBACK_US); /* A push landing now must not touch these bytes. */

    bool torn = b->size != size_for(value);
    for (size_t i = 0; i < b->size && !torn; i++)
        torn = bytes[i] != value;
    if (torn)
        atomic_fetch_add(&g_data_torn, 1);
    atomic_fetch_add(&g_data_fresh, 1);
}

static void test_delivered_data_is_stable(void)
{
    struct pwf_filter* filter = pwf_filter_create("pwf-test-push-race-data", data_cb, NULL);
    PWF_ASSERT(filter != NULL);
    struct pwf_filter_port* in = pwf_filter_add_signal_port(filter, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(in != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    uint8_t buf[16 + 255 * 8];
    for (unsigned i = 0; i < PUSHES; i++) {
        /* Sizes vary, so the push side regrows its buffer while capacity catches up. */
        uint8_t value = (uint8_t)(1 + (i * 37) % 255);
        memset(buf, value, size_for(value));
        PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, in, buf, size_for(value), i), PWF_OK);
        usleep(300 + (i % 7) * 300);
    }

    bool delivered = wait_for(&g_data_fresh, 1);
    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);

    unsigned torn = atomic_load(&g_data_torn);
    PWF_ASSERT(delivered);
    PWF_ASSERT_EQ(torn, 0u);
}

int main(void)
{
    test_no_event_is_lost();
    test_delivered_data_is_stable();
    printf("test_filter_push_race: all cases passed\n");
    return 0;
}
