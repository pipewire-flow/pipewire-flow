/* SPDX-License-Identifier: MIT */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <pipewire/keys.h>

#include "pwf_test_hw_discover.h"

#define PWF_DISCOVER_TIMEOUT_NSEC (3 * SPA_NSEC_PER_SEC)

struct discover_state {
    struct pw_thread_loop* loop;
    struct pw_core* core;
    struct pw_registry* registry;
    struct spa_hook core_listener;
    struct spa_hook registry_listener;

    const char* want_class;
    char* out;
    size_t out_size;
    bool found;

    int pending_seq;
    bool sync_done;
};

static void on_global(void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version,
                       const struct spa_dict* props)
{
    struct discover_state* st = data;
    (void)id;
    (void)permissions;
    (void)version;

    if (st->found || !props || strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
        return;

    const char* klass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (!klass || !name || strcmp(klass, st->want_class) != 0)
        return;
    if (strlen(name) >= st->out_size)
        return;

    strcpy(st->out, name);
    st->found = true;
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_global,
};

static void on_done(void* data, uint32_t id, int seq)
{
    struct discover_state* st = data;
    if (id == PW_ID_CORE && seq == st->pending_seq) {
        st->sync_done = true;
        pw_thread_loop_signal(st->loop, false);
    }
}

static void on_error(void* data, uint32_t id, int seq, int res, const char* message)
{
    struct discover_state* st = data;
    (void)id;
    (void)seq;
    (void)res;
    (void)message;
    st->sync_done = true;
    pw_thread_loop_signal(st->loop, false);
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_done,
    .error = on_error,
};

bool pwf_test_find_node(const char* media_class, char* out, size_t out_size)
{
    if (!media_class || !out || out_size == 0)
        return false;
    out[0] = '\0';

    struct discover_state st = { .want_class = media_class, .out = out, .out_size = out_size };

    pw_init(NULL, NULL);
    st.loop = pw_thread_loop_new("pwf-discover", NULL);
    if (!st.loop) {
        pw_deinit();
        return false;
    }

    struct pw_context* context = NULL;
    if (pw_thread_loop_start(st.loop) == 0) {
        pw_thread_loop_lock(st.loop);
        context = pw_context_new(pw_thread_loop_get_loop(st.loop), NULL, 0);
        st.core = context ? pw_context_connect(context, NULL, 0) : NULL;

        if (st.core) {
            pw_core_add_listener(st.core, &st.core_listener, &core_events, &st);
            st.registry = pw_core_get_registry(st.core, PW_VERSION_REGISTRY, 0);
            if (st.registry)
                pw_registry_add_listener(st.registry, &st.registry_listener, &registry_events, &st);

            /* One round-trip is enough: the registry replays every existing
             * global before answering the sync. */
            st.pending_seq = pw_core_sync(st.core, PW_ID_CORE, 0);
            struct timespec deadline;
            pw_thread_loop_get_time(st.loop, &deadline, PWF_DISCOVER_TIMEOUT_NSEC);
            while (!st.sync_done) {
                if (pw_thread_loop_timed_wait_full(st.loop, &deadline) < 0)
                    break;
            }
            if (st.registry)
                spa_hook_remove(&st.registry_listener);
            spa_hook_remove(&st.core_listener);
        }
        pw_thread_loop_unlock(st.loop);
    }

    pw_thread_loop_stop(st.loop);
    if (st.registry)
        pw_proxy_destroy((struct pw_proxy*)st.registry);
    if (st.core)
        pw_core_disconnect(st.core);
    if (context)
        pw_context_destroy(context);
    pw_thread_loop_destroy(st.loop);
    pw_deinit();

    return st.found;
}
