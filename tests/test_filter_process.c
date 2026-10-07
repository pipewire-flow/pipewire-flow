/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "pwf/pwf_filter.h"
#include "pwf_test.h"

static int g_cycles = 0;
static size_t g_last_n_buffers = 0;
static int64_t g_last_output_pts = 0;

static void process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                       void* user_data)
{
    (void)filter;
    (void)user_data;
    g_cycles++;
    g_last_n_buffers = n_buffers;
    /* Output buffers are left untouched (size stays 0); this must not
     * be treated as an error by the library. Port 2 (added last) is
     * the output port; pts is always -1 there, per pwf_filter.h. */
    if (n_buffers > 2)
        g_last_output_pts = buffers[2].pts;
}

static int g_mixed_cycles = 0;
static size_t g_mixed_n_buffers = 0;
static int64_t g_mixed_event_pts = 0;

static void mixed_process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                             void* user_data)
{
    (void)filter;
    (void)user_data;
    g_mixed_cycles++;
    g_mixed_n_buffers = n_buffers;
    /* Port 3 (added last) is the event port; pts is always -1 there,
     * per pwf_filter.h. */
    if (n_buffers > 3)
        g_mixed_event_pts = buffers[3].pts;
}

static int g_dmabuf_mixed_cycles = 0;
static size_t g_dmabuf_mixed_n_buffers = 0;

static void dmabuf_mixed_process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                                    void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)user_data;
    g_dmabuf_mixed_cycles++;
    g_dmabuf_mixed_n_buffers = n_buffers;
}

static int g_many_cycles = 0;
static size_t g_many_n_buffers = 0;

static void many_ports_process_cb(struct pwf_filter* filter, struct pwf_filter_port_buffer* buffers, size_t n_buffers,
                                   void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)user_data;
    g_many_cycles++;
    g_many_n_buffers = n_buffers;
}

int main(void)
{
    struct pwf_filter* filter = pwf_filter_create("pwf-test-process", process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    struct pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };
    PWF_ASSERT(pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT, &cfg) != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_INPUT, &cfg) != NULL);
    PWF_ASSERT(pwf_filter_add_audio_port(filter, PWF_FILTER_PORT_OUTPUT, &cfg) != NULL);

    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);
    sleep(1);

    /* All three ports' buffers must arrive together, every cycle. */
    PWF_ASSERT(g_cycles > 0);
    PWF_ASSERT_EQ(g_last_n_buffers, (size_t)3);
    PWF_ASSERT_EQ(g_last_output_pts, (int64_t)-1);

    /* stop() must actually halt delivery, not just stop returning new
     * cycles eventually — no further cycles once stopped. */
    pwf_filter_stop(filter, false);
    int cycles_after_stop = g_cycles;
    sleep(1);
    PWF_ASSERT_EQ(g_cycles, cycles_after_stop);

    pwf_filter_destroy(filter);

    /* One port of each of the four supported kinds on a single filter
     * must still be delivered together in one callback invocation per
     * cycle (audio/video/signal/event mixing). */
    struct pwf_filter* mixed = pwf_filter_create("pwf-test-process-mixed", mixed_process_cb, NULL);
    PWF_ASSERT(mixed != NULL);

    PWF_ASSERT(pwf_filter_add_audio_port(mixed, PWF_FILTER_PORT_INPUT, &cfg) != NULL);
    struct pwf_video_config vcfg = { .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 };
    PWF_ASSERT(pwf_filter_add_video_port(mixed, PWF_FILTER_PORT_INPUT, &vcfg) != NULL);
    PWF_ASSERT(pwf_filter_add_signal_port(mixed, PWF_FILTER_PORT_INPUT) != NULL);
    PWF_ASSERT(pwf_filter_add_event_port(mixed, PWF_FILTER_PORT_INPUT) != NULL);

    PWF_ASSERT_EQ(pwf_filter_start(mixed), PWF_OK);
    sleep(1);

    PWF_ASSERT(g_mixed_cycles > 0);
    PWF_ASSERT_EQ(g_mixed_n_buffers, (size_t)4);
    PWF_ASSERT_EQ(g_mixed_event_pts, (int64_t)-1);

    pwf_filter_stop(mixed, false);
    pwf_filter_destroy(mixed);

    /* A DMABUF video input port mixed with audio and signal ports must
     * still be delivered together in one callback per cycle, even with no
     * DMABUF source linked — the video port simply carries no frame that
     * cycle. */
    struct pwf_filter* dmabuf_mixed = pwf_filter_create("pwf-test-process-dmabuf", dmabuf_mixed_process_cb, NULL);
    PWF_ASSERT(dmabuf_mixed != NULL);

    PWF_ASSERT(pwf_filter_add_audio_port(dmabuf_mixed, PWF_FILTER_PORT_INPUT, &cfg) != NULL);
    struct pwf_filter_port_opts dmabuf_opts = { .memory = PWF_PORT_MEMORY_DMABUF };
    PWF_ASSERT(pwf_filter_add_video_port_ex(dmabuf_mixed, PWF_FILTER_PORT_INPUT, &vcfg, &dmabuf_opts) != NULL);
    PWF_ASSERT(pwf_filter_add_signal_port(dmabuf_mixed, PWF_FILTER_PORT_INPUT) != NULL);

    PWF_ASSERT_EQ(pwf_filter_start(dmabuf_mixed), PWF_OK);
    sleep(1);

    PWF_ASSERT(g_dmabuf_mixed_cycles > 0);
    PWF_ASSERT_EQ(g_dmabuf_mixed_n_buffers, (size_t)3);

    pwf_filter_stop(dmabuf_mixed, false);
    pwf_filter_destroy(dmabuf_mixed);

    /* More ports than the internal stack-allocation threshold (8) must
     * fall back to heap allocation for the per-cycle buffer array
     * without breaking delivery. */
    struct pwf_filter* many = pwf_filter_create("pwf-test-process-many", many_ports_process_cb, NULL);
    PWF_ASSERT(many != NULL);

    const size_t n_ports = 9;
    for (size_t i = 0; i < n_ports; i++)
        PWF_ASSERT(pwf_filter_add_audio_port(many, PWF_FILTER_PORT_INPUT, &cfg) != NULL);

    PWF_ASSERT_EQ(pwf_filter_start(many), PWF_OK);
    sleep(1);

    PWF_ASSERT(g_many_cycles > 0);
    PWF_ASSERT_EQ(g_many_n_buffers, n_ports);

    pwf_filter_stop(many, false);
    pwf_filter_destroy(many);
    return 0;
}
