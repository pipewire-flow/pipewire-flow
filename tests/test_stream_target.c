/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "pwf/pwf_stream.h"
#include "pwf_test.h"

static void ignore_data_cb(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

int main(void)
{
    /* NULL handle is rejected regardless of target. */
    PWF_ASSERT_EQ(pwf_stream_set_target(NULL, "some-node"), PWF_ERR_INVALID_ARG);

    /* A target naming a node that doesn't exist must not break the
     * connect/start/stop lifecycle: PW_KEY_TARGET_OBJECT only steers
     * PipeWire's auto-link policy, it isn't validated at connect time. */
    pwf_stream_h s = pwf_stream_create(PWF_DATA_AUDIO, ignore_data_cb, NULL);
    PWF_ASSERT(s != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_target(s, "pwf-test-nonexistent-node"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &(pwf_audio_config){ .sample_rate = 48000, .channels = 2 }), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_start(s), PWF_OK);
    sleep(1);
    PWF_ASSERT_EQ(pwf_stream_stop(s, false), PWF_OK);

    /* Clearing back to NULL (falls back to auto-connect) is also accepted. */
    PWF_ASSERT_EQ(pwf_stream_set_target(s, NULL), PWF_OK);
    pwf_stream_destroy(s);

    return 0;
}
