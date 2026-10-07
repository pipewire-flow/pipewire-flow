/* SPDX-License-Identifier: MIT */

/* A role is a hint no daemon checks, so what can be verified without a
 * session manager is that it lands on the node, and only when asked for. */

#include <string.h>

#include "pwf_stream_internal.h"
#include "pwf_test.h"

static void on_data(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void on_fill(pwf_stream_h stream, pwf_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    buf->size = 0;
}

static const pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2 };

/* Reads media.role off the connected node, copying it out under the lock. */
static bool node_role(pwf_stream_h handle, char* out, size_t out_size)
{
    struct pwf_stream* stream = (struct pwf_stream*)handle;
    pw_thread_loop_lock(stream->conn.loop);
    const struct pw_properties* props = pw_stream_get_properties(stream->pw_stream);
    const char* role = props ? pw_properties_get(props, PW_KEY_MEDIA_ROLE) : NULL;
    if (role)
        snprintf(out, out_size, "%s", role);
    pw_thread_loop_unlock(stream->conn.loop);
    return role != NULL;
}

/* A role can be set, replaced and cleared, and NULL and "" both clear it. */
static void test_set_and_clear(void)
{
    PWF_ASSERT_EQ(pwf_stream_set_role(NULL, "Music"), PWF_ERR_INVALID_ARG);

    pwf_stream_h s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    struct pwf_stream* stream = (struct pwf_stream*)s;
    PWF_ASSERT(stream->role == NULL); /* no role until told otherwise */

    PWF_ASSERT_EQ(pwf_stream_set_role(s, "Music"), PWF_OK);
    PWF_ASSERT_EQ(strcmp(stream->role, "Music"), 0);
    PWF_ASSERT_EQ(pwf_stream_set_role(s, "Communication"), PWF_OK);
    PWF_ASSERT_EQ(strcmp(stream->role, "Communication"), 0);

    PWF_ASSERT_EQ(pwf_stream_set_role(s, NULL), PWF_OK);
    PWF_ASSERT(stream->role == NULL);
    PWF_ASSERT_EQ(pwf_stream_set_role(s, "Music"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_role(s, ""), PWF_OK);
    PWF_ASSERT(stream->role == NULL);

    pwf_stream_destroy(s);
}

/* Unlike a target, a role does not contradict manual wiring, so neither
 * order of the two setters is refused. */
static void test_accepted_with_autoconnect_off(void)
{
    pwf_stream_h s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_role(s, "Music"), PWF_OK);
    pwf_stream_destroy(s);

    s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_role(s, "Music"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_autoconnect(s, false), PWF_OK);
    pwf_stream_destroy(s);
}

/* The role reaches the node the format connects, on capture and playback
 * alike, and a stream that set none declares none. */
static void test_role_reaches_the_node(void)
{
    char role[64];

    pwf_stream_h s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT(!node_role(s, role, sizeof(role)));
    pwf_stream_destroy(s);

    s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    PWF_ASSERT(s != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_role(s, "Communication"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(s, &cfg), PWF_OK);
    PWF_ASSERT(node_role(s, role, sizeof(role)));
    PWF_ASSERT_EQ(strcmp(role, "Communication"), 0);
    pwf_stream_destroy(s);

    pwf_stream_h p = pwf_stream_create_playback(on_fill, NULL);
    PWF_ASSERT(p != NULL);
    PWF_ASSERT_EQ(pwf_stream_set_role(p, "Music"), PWF_OK);
    PWF_ASSERT_EQ(pwf_stream_set_audio_config(p, &cfg), PWF_OK);
    PWF_ASSERT(node_role(p, role, sizeof(role)));
    PWF_ASSERT_EQ(strcmp(role, "Music"), 0);
    pwf_stream_destroy(p);
}

int main(void)
{
    test_set_and_clear();
    test_accepted_with_autoconnect_off();
    test_role_reaches_the_node();
    printf("test_stream_role: all cases passed\n");
    return 0;
}
