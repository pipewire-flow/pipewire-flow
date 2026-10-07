/* SPDX-License-Identifier: MIT */

/* Link and unlink rejections, plus the source-unavailable notification.
 * Every link here names a node that deliberately does not exist, so no
 * hardware is needed; successful links live in the hardware suite. */

#include <pipewire/link.h>

#include "pwf_filter_internal.h"
#include "pwf_test.h"

/* A node name no real PipeWire graph will have. */
#define ABSENT_NODE "pwf-no-such-node-12345"

static int g_error_calls = 0;
static pwf_filter_port_h g_last_port = NULL;
static int g_last_error_code = 0;

static void noop_process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers,
                             void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

static void on_error(pwf_filter_h filter, pwf_filter_port_h port, int error_code, void* user_data)
{
    (void)filter;
    (void)user_data;
    g_error_calls++;
    g_last_port = port;
    g_last_error_code = error_code;
}

static const pwf_audio_config g_audio_cfg = { .sample_rate = 48000, .channels = 2 };

/* Argument validation that needs no started filter. */
static void test_invalid_args(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-link-args", noop_process_cb, NULL);
    PWF_ASSERT(handle != NULL);

    pwf_filter_port_h in = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &g_audio_cfg);
    pwf_filter_port_h out = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_OUTPUT, &g_audio_cfg);
    PWF_ASSERT(in != NULL && out != NULL);

    /* NULL handles and empty targets are rejected before anything else. */
    PWF_ASSERT_EQ(pwf_filter_port_link(NULL, ABSENT_NODE), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_link(in, NULL), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_link(in, ""), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_unlink(NULL), PWF_ERR_INVALID_ARG);

    /* Output ports are out of scope for this feature, in both directions
     * of the API, and are rejected before the filter's state matters. */
    PWF_ASSERT_EQ(pwf_filter_port_link(out, ABSENT_NODE), PWF_ERR_INVALID_ARG);
    PWF_ASSERT_EQ(pwf_filter_port_unlink(out), PWF_ERR_INVALID_ARG);

    /* Linking needs a live graph, so it is the one port call that must come
     * after start() rather than before it. */
    PWF_ASSERT_EQ(pwf_filter_port_link(in, ABSENT_NODE), PWF_ERR_NOT_CONFIGURED);

    pwf_filter_destroy(handle);
}

/* Target resolution failures, which require a started filter. */
static void test_unresolvable_target(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-link-target", noop_process_cb, NULL);
    PWF_ASSERT(handle != NULL);

    pwf_filter_port_h in = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &g_audio_cfg);
    PWF_ASSERT(in != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(handle), PWF_OK);

    /* Neither a bare node name nor its "node:port" form resolves. */
    PWF_ASSERT_EQ(pwf_filter_port_link(in, ABSENT_NODE), PWF_ERR_NOT_FOUND);
    PWF_ASSERT_EQ(pwf_filter_port_link(in, ABSENT_NODE ":capture_FL"), PWF_ERR_NOT_FOUND);
    /* An all-digit target is read as an object.serial; this one is nobody's. */
    PWF_ASSERT_EQ(pwf_filter_port_link(in, "4294967290"), PWF_ERR_NOT_FOUND);

    /* A failed link leaves no partial state behind, so there is still
     * nothing to unlink. */
    PWF_ASSERT_EQ(pwf_filter_port_unlink(in), PWF_ERR_NOT_CONFIGURED);

    pwf_filter_stop(handle, false);
    pwf_filter_destroy(handle);
}

/* Unlink's own state transitions, and a stop/start/destroy cycle over a
 * filter whose every link attempt failed. */
static void test_unlink_states(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-unlink", noop_process_cb, NULL);
    PWF_ASSERT(handle != NULL);

    pwf_filter_port_h in = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &g_audio_cfg);
    PWF_ASSERT(in != NULL);

    /* Never linked, before or after start. */
    PWF_ASSERT_EQ(pwf_filter_port_unlink(in), PWF_ERR_NOT_CONFIGURED);
    PWF_ASSERT_EQ(pwf_filter_start(handle), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_port_unlink(in), PWF_ERR_NOT_CONFIGURED);

    /* A restart is clean, and linking is still rejected while stopped. */
    PWF_ASSERT_EQ(pwf_filter_stop(handle, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_port_link(in, ABSENT_NODE), PWF_ERR_NOT_CONFIGURED);
    PWF_ASSERT_EQ(pwf_filter_start(handle), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_port_unlink(in), PWF_ERR_NOT_CONFIGURED);

    pwf_filter_stop(handle, false);
    pwf_filter_destroy(handle);
}

/* Whitebox: drive the link's info callback directly to prove a link that
 * was up and then died is reported through the filter's error callback.
 * Physically removing a device is not something a unit test can do. */
static void test_source_unavailable_notification(void)
{
    pwf_filter_h handle = pwf_filter_create("pwf-test-link-notify", noop_process_cb, NULL);
    PWF_ASSERT(handle != NULL);
    PWF_ASSERT_EQ(pwf_filter_set_error_cb(handle, on_error), PWF_OK);

    pwf_filter_port_h port_a = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &g_audio_cfg);
    pwf_filter_port_h port_b = pwf_filter_add_audio_port(handle, PWF_FILTER_PORT_INPUT, &g_audio_cfg);
    PWF_ASSERT(port_a != NULL && port_b != NULL);
    PWF_ASSERT_EQ(pwf_filter_start(handle), PWF_OK);

    struct pwf_filter_port* a = (struct pwf_filter_port*)port_a;
    struct pwf_filter_port* b = (struct pwf_filter_port*)port_b;

    struct pw_link_info up = { .change_mask = PW_LINK_CHANGE_MASK_STATE, .state = PW_LINK_STATE_ACTIVE };
    struct pw_link_info gone = { .change_mask = PW_LINK_CHANGE_MASK_STATE, .state = PW_LINK_STATE_UNLINKED };
    struct pw_link_info no_state = { .change_mask = PW_LINK_CHANGE_MASK_PROPS, .state = PW_LINK_STATE_UNLINKED };

    /* A link that never came up going away is a failed negotiation, not a
     * lost source, so it must stay silent. */
    pwf_filter_link_on_info(b, &gone);
    PWF_ASSERT_EQ(g_error_calls, 0);

    /* Once a link has been up, losing it is a lost source. */
    pwf_filter_link_on_info(a, &up);
    PWF_ASSERT(a->link_state_seen_active);
    pwf_filter_link_on_info(a, &gone);

    PWF_ASSERT_EQ(g_error_calls, 1);
    PWF_ASSERT(g_last_port == port_a);
    PWF_ASSERT_EQ(g_last_error_code, PWF_ERR_SOURCE_UNAVAILABLE);

    /* The port is no longer considered linked, so a repeat report is silent
     * and there is nothing left to unlink. */
    pwf_filter_link_on_info(a, &gone);
    PWF_ASSERT_EQ(g_error_calls, 1);
    PWF_ASSERT_EQ(pwf_filter_port_unlink(port_a), PWF_ERR_NOT_CONFIGURED);

    /* An info event that carries no state change is ignored entirely. */
    pwf_filter_link_on_info(a, &up);
    pwf_filter_link_on_info(a, &no_state);
    PWF_ASSERT_EQ(g_error_calls, 1);

    pwf_filter_stop(handle, false);
    pwf_filter_destroy(handle);
}

int main(void)
{
    test_invalid_args();
    test_unresolvable_target();
    test_unlink_states();
    test_source_unavailable_notification();
    return 0;
}
