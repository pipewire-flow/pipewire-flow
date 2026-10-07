/* SPDX-License-Identifier: MIT */

/* Whitebox test: exercises the state_changed -> error_cb wiring directly
 * by simulating a PipeWire stream-state transition, rather than
 * physically removing a device. A real end-to-end device-loss check is
 * documented separately in the project's quickstart guide. */

#include "pwf_stream_internal.h"
#include "pwf_test.h"

static int g_error_calls = 0;
static int g_last_error_code = 0;

static void noop_data_cb(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void on_error(pwf_stream_h stream, int error_code, void* user_data)
{
    (void)stream;
    (void)user_data;
    g_error_calls++;
    g_last_error_code = error_code;
}

static void check_source_loss(pwf_stream_h handle)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;

    stream->state = PWF_STREAM_STATE_RUNNING;
    g_error_calls = 0;

    pwf_stream_on_state_changed(stream, PW_STREAM_STATE_STREAMING, PW_STREAM_STATE_ERROR,
                                 "simulated source loss");

    PWF_ASSERT_EQ(g_error_calls, 1);
    PWF_ASSERT_EQ(g_last_error_code, PWF_ERR_SOURCE_UNAVAILABLE);
    PWF_ASSERT_EQ(stream->state, PWF_STREAM_STATE_STOPPED);

    /* No further error is raised once already stopped. */
    pwf_stream_on_state_changed(stream, PW_STREAM_STATE_UNCONNECTED, PW_STREAM_STATE_ERROR,
                                 "simulated source loss again");
    PWF_ASSERT_EQ(g_error_calls, 1);
}

int main(void)
{
    pwf_stream_h audio = pwf_stream_create(PWF_DATA_AUDIO, noop_data_cb, NULL);
    PWF_ASSERT(audio != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_error_cb(audio, on_error), PWF_OK);
    check_source_loss(audio);
    pwf_stream_destroy(audio);

    pwf_stream_h video = pwf_stream_create(PWF_DATA_VIDEO, noop_data_cb, NULL);
    PWF_ASSERT(video != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_error_cb(video, on_error), PWF_OK);
    check_source_loss(video);
    pwf_stream_destroy(video);

    return 0;
}
