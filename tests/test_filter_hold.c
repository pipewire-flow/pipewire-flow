/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "pwf/pwf_filter.h"
#include "pwf_test.h"

/* Two signal input ports: one with hold enabled, one without. The process
 * callback fires continuously once started; counters are cumulative and
 * monotonic so they can be read from the main thread without resetting. */
static pwf_filter_port_h g_hold_port;
static pwf_filter_port_h g_nohold_port;

static int g_hold_fresh;       /* new-data deliveries on the hold port */
static int g_hold_held;        /* held re-presentations on the hold port */
static int g_hold_nodata;      /* cycles the hold port had no buffer */
static uint64_t g_hold_fresh_seq;   /* seq seen on the latest fresh delivery */
static uint64_t g_hold_held_seq;    /* seq seen on the latest held delivery */
static size_t g_hold_held_size;
static int64_t g_hold_held_pts;

static int g_nohold_fresh;
static int g_nohold_held;      /* MUST stay 0: hold is opt-in */

static void process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    (void)user_data;
    for (size_t i = 0; i < n_buffers; i++) {
        pwf_filter_port_buffer* b = &buffers[i];
        bool is_hold = (b->port == g_hold_port);

        if (!b->data) {
            if (is_hold)
                g_hold_nodata++;
            continue;
        }
        if (b->fresh) {
            if (is_hold) {
                g_hold_fresh++;
                g_hold_fresh_seq = b->seq;
            } else {
                g_nohold_fresh++;
            }
        } else {
            if (is_hold) {
                g_hold_held++;
                g_hold_held_seq = b->seq;
                g_hold_held_size = b->size;
                g_hold_held_pts = b->pts;
            } else {
                g_nohold_held++;
            }
        }
    }
}

int main(void)
{
    pwf_filter_h filter = pwf_filter_create("pwf-test-hold", process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    g_hold_port = pwf_filter_add_signal_port(filter, PWF_FILTER_PORT_INPUT);
    g_nohold_port = pwf_filter_add_signal_port(filter, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(g_hold_port != NULL);
    PWF_ASSERT(g_nohold_port != NULL);

    /* Hold is opt-in and only valid before start. */
    PWF_ASSERT_EQ(pwf_filter_port_set_hold(g_hold_port, true), PWF_OK);

    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    /* Setting hold after start is rejected. */
    PWF_ASSERT_EQ(pwf_filter_port_set_hold(g_hold_port, true), PWF_ERR_INVALID_ARG);

    /* Phase A: before any buffer, a hold port reports no buffer (not a
     * stale/invalid one) — same as a non-hold port. */
    usleep(400000);
    PWF_ASSERT_EQ(g_hold_fresh, 0);
    PWF_ASSERT_EQ(g_hold_held, 0);
    PWF_ASSERT(g_hold_nodata > 0);

    /* Phase B: one push to each port. Exactly one fresh delivery per push;
     * the hold port then re-presents that buffer on later cycles. */
    float v1 = 0.25f;
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, g_hold_port, &v1, sizeof(v1), 1000), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, g_nohold_port, &v1, sizeof(v1), 1000), PWF_OK);
    usleep(500000);

    PWF_ASSERT_EQ(g_hold_fresh, 1);
    PWF_ASSERT_EQ(g_hold_fresh_seq, 1);
    PWF_ASSERT(g_hold_held > 0);          /* re-presented on empty cycles */
    PWF_ASSERT_EQ(g_hold_held_seq, 1);    /* seq unchanged while held */
    PWF_ASSERT_EQ(g_hold_held_size, sizeof(float));
    PWF_ASSERT_EQ(g_hold_held_pts, (int64_t)1000);

    PWF_ASSERT_EQ(g_nohold_fresh, 1);
    PWF_ASSERT_EQ(g_nohold_held, 0);      /* non-hold never re-presents */

    /* Phase C: a new push advances seq and updates the held payload. */
    float v2 = 0.75f;
    PWF_ASSERT_EQ(pwf_filter_push_port_data(filter, g_hold_port, &v2, sizeof(v2), 2000), PWF_OK);
    usleep(500000);

    PWF_ASSERT_EQ(g_hold_fresh, 2);
    PWF_ASSERT_EQ(g_hold_fresh_seq, 2);
    PWF_ASSERT_EQ(g_hold_held_seq, 2);
    PWF_ASSERT_EQ(g_hold_held_pts, (int64_t)2000);
    PWF_ASSERT_EQ(g_nohold_held, 0);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);
    return 0;
}
