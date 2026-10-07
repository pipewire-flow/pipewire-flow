/* SPDX-License-Identifier: MIT */

/* Pushing from inside the processing callback must not take the filter's
 * thread-loop lock — doing so deadlocks against PipeWire's buffer setup.
 * The lock is skipped based on pwf_filter_processing, so these tests pin
 * that marker's behaviour directly instead of relying on a hang, which
 * only reproduces under parallel load and would pass on an idle machine. */

#include <unistd.h>

#include "pwf_filter_internal.h"
#include "pwf_test.h"

static const pwf_audio_config g_cfg = { .sample_rate = 48000, .channels = 2 };

/* --- marker is set during the callback and restored after ------------- */

static struct pwf_filter* g_expect;
static const struct pwf_filter* g_seen;
static int g_marker_calls;

static void marker_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)buffers;
    (void)n;
    (void)user_data;
    (void)filter;
    g_marker_calls++;
    g_seen = pwf_filter_processing;
}

static void test_marker_set_and_restored(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-cb-marker", marker_cb, NULL);
    PWF_ASSERT(handle != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &g_cfg) != NULL);

    struct pwf_filter* filter = (struct pwf_filter*)handle;
    g_expect = filter;

    /* Nothing is running on this thread yet. */
    PWF_ASSERT(pwf_filter_processing == NULL);

    /* Drive the process callback directly so the whole set/restore happens
     * on this thread, where it can be observed. */
    pwf_filter_on_process(filter, NULL);

    PWF_ASSERT_EQ(g_marker_calls, 1);
    PWF_ASSERT(g_seen == filter);           /* set while the callback ran */
    PWF_ASSERT(pwf_filter_processing == NULL); /* and restored afterwards */

    pwf_filter_destroy(handle);
}

/* --- a nested filter restores the outer one, not NULL ----------------- */

static struct pwf_filter* g_inner;
static const struct pwf_filter* g_seen_inner;
static const struct pwf_filter* g_seen_after_inner;

static void inner_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n;
    (void)user_data;
    g_seen_inner = pwf_filter_processing;
}

static void outer_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n;
    (void)user_data;
    pwf_filter_on_process(g_inner, NULL);
    g_seen_after_inner = pwf_filter_processing;
}

static void test_nested_filter_restores_outer(void)
{
    pwf_filter_h outer_h = pwf_filter_create("pwf-test-cb-outer", outer_cb, NULL);
    pwf_filter_h inner_h = pwf_filter_create("pwf-test-cb-inner", inner_cb, NULL);
    PWF_ASSERT(outer_h != NULL && inner_h != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(outer_h, PWF_FILTER_PORT_INPUT, &g_cfg) != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(inner_h, PWF_FILTER_PORT_INPUT, &g_cfg) != NULL);

    struct pwf_filter* outer = (struct pwf_filter*)outer_h;
    g_inner = (struct pwf_filter*)inner_h;

    pwf_filter_on_process(outer, NULL);

    /* Each callback sees its own filter, and the inner one hands the marker
     * back to the outer rather than clearing it — otherwise a push in the
     * rest of the outer callback would wrongly take the lock. */
    PWF_ASSERT(g_seen_inner == g_inner);
    PWF_ASSERT(g_seen_after_inner == outer);
    PWF_ASSERT(pwf_filter_processing == NULL);

    pwf_filter_destroy(outer_h);
    pwf_filter_destroy(inner_h);
}

/* --- push_port_data from inside the callback ------------------------- */

static pwf_filter_port_h g_push_port;
static int g_push_result = 1;      /* 1 = never attempted */
static int g_push_cycles;
static float g_delivered = -1.0f;

static void push_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)user_data;
    g_push_cycles++;

    for (size_t i = 0; i < n; i++) {
        if (buffers[i].port == g_push_port && buffers[i].data && buffers[i].size >= sizeof(float))
            g_delivered = *(const float*)buffers[i].data;
    }

    /* Push once, from inside the callback: this is the path that used to
     * take the loop lock on the data thread and deadlock. */
    if (g_push_result == 1) {
        float v = 0.5f;
        g_push_result = pwf_filter_push_port_data(filter, g_push_port, &v, sizeof(v), 42);
    }
}

static void test_push_port_data_from_callback(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-cb-push", push_cb, NULL);
    PWF_ASSERT(handle != NULL);
    g_push_port = pwf_filter_add_signal_port(handle, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(g_push_port != NULL);

    struct pwf_filter* filter = (struct pwf_filter*)handle;

    /* Two cycles: the first pushes, the second must see what it staged. */
    pwf_filter_on_process(filter, NULL);
    PWF_ASSERT_EQ(g_push_result, PWF_OK);
    pwf_filter_on_process(filter, NULL);

    PWF_ASSERT_EQ(g_push_cycles, 2);
    PWF_ASSERT_EQ(g_delivered, 0.5f);

    pwf_filter_destroy(handle);
}

/* --- pushing to a different filter still locks normally --------------- */

static struct pwf_filter* g_other;
static pwf_filter_port_h g_other_port;
static int g_cross_result = 1;

static void cross_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n;
    (void)user_data;
    if (g_cross_result == 1) {
        float v = 1.5f;
        /* A different filter's loop is not the one running us, so this one
         * does take its lock — it must still succeed and not hang. */
        g_cross_result = pwf_filter_push_port_data((pwf_filter_h)g_other, g_other_port, &v, sizeof(v), -1);
    }
}

static void test_push_to_other_filter_from_callback(void)
{
    pwf_filter_h driver = pwf_filter_create("pwf-test-cb-cross-a", cross_cb, NULL);
    pwf_filter_h other = pwf_filter_create("pwf-test-cb-cross-b", marker_cb, NULL);
    PWF_ASSERT(driver != NULL && other != NULL);
    PWF_ASSERT(pwf_filter_add_signal_port(driver, PWF_FILTER_PORT_INPUT) != NULL);
    g_other_port = pwf_filter_add_signal_port(other, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(g_other_port != NULL);
    g_other = (struct pwf_filter*)other;

    pwf_filter_on_process((struct pwf_filter*)driver, NULL);
    PWF_ASSERT_EQ(g_cross_result, PWF_OK);

    pwf_filter_destroy(driver);
    pwf_filter_destroy(other);
}

/* --- app-thread pushes still work while the filter runs --------------- */

static void quiet_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n;
    (void)user_data;
}

static void test_app_thread_push_still_works(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-cb-appthread", quiet_cb, NULL);
    PWF_ASSERT(handle != NULL);
    pwf_filter_port_h in = pwf_filter_add_signal_port(handle, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(in != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(handle), PWF_OK);

    /* The marker is per-thread, so an app-thread push takes the lock as
     * before even while the filter's own thread is processing. */
    float v = 2.5f;
    PWF_ASSERT(pwf_filter_processing == NULL);
    PWF_ASSERT_EQ(pwf_filter_push_port_data(handle, in, &v, sizeof(v), -1), PWF_OK);

    pwf_filter_stop(handle, false);
    pwf_filter_destroy(handle);
}

int main(void)
{
    test_marker_set_and_restored();
    test_nested_filter_restores_outer();
    test_push_port_data_from_callback();
    test_push_to_other_filter_from_callback();
    test_app_thread_push_still_works();
    return 0;
}
