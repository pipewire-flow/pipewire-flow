/* SPDX-License-Identifier: MIT */

#ifndef TPW_PW_DL_H
#define TPW_PW_DL_H

/* libpipewire is opened at run time rather than linked, so a process pays
 * nothing for it before its first stream or filter and gets it all back
 * after its last one. Every library source that calls libpipewire includes
 * this header after all other headers: it holds one pointer per function the
 * library uses and maps each name onto its pointer, so call sites stay
 * ordinary pw_* calls. The tests call libpipewire directly and never include
 * it. */

#include <pipewire/impl-module.h>
#include <pipewire/pipewire.h>

/* Every libpipewire function the library calls. The link fails on any call
 * missing from here, since the library no longer links libpipewire. */
#define TPW_PW_FUNCS(X) \
    X(pw_context_add_spa_lib) \
    X(pw_context_connect) \
    X(pw_context_destroy) \
    X(pw_context_load_module) \
    X(pw_context_new) \
    X(pw_core_disconnect) \
    X(pw_deinit) \
    X(pw_filter_add_listener) \
    X(pw_filter_add_port) \
    X(pw_filter_connect) \
    X(pw_filter_dequeue_buffer) \
    X(pw_filter_destroy) \
    X(pw_filter_flush) \
    X(pw_filter_get_node_id) \
    X(pw_filter_new) \
    X(pw_filter_queue_buffer) \
    X(pw_filter_set_active) \
    X(pw_filter_update_params) \
    X(pw_filter_update_properties) \
    X(pw_init) \
    X(pw_properties_free) \
    X(pw_properties_new) \
    X(pw_properties_set) \
    X(pw_proxy_add_object_listener) \
    X(pw_proxy_destroy) \
    X(pw_stream_add_listener) \
    X(pw_stream_connect) \
    X(pw_stream_dequeue_buffer) \
    X(pw_stream_destroy) \
    X(pw_stream_flush) \
    X(pw_stream_get_node_id) \
    X(pw_stream_get_time_n) \
    X(pw_stream_new) \
    X(pw_stream_queue_buffer) \
    X(pw_stream_set_active) \
    X(pw_stream_update_params) \
    X(pw_thread_loop_destroy) \
    X(pw_thread_loop_get_loop) \
    X(pw_thread_loop_get_time) \
    X(pw_thread_loop_in_thread) \
    X(pw_thread_loop_lock) \
    X(pw_thread_loop_new) \
    X(pw_thread_loop_signal) \
    X(pw_thread_loop_start) \
    X(pw_thread_loop_stop) \
    X(pw_thread_loop_timed_wait_full) \
    X(pw_thread_loop_unlock)

struct tpw_pw_fns {
#define TPW_PW_FN_SLOT(name) __typeof__(name)* name;
    TPW_PW_FUNCS(TPW_PW_FN_SLOT)
#undef TPW_PW_FN_SLOT
};

/* Filled by tpw_pw_dl_open(), zeroed by tpw_pw_dl_close(). */
extern struct tpw_pw_fns tpw_pw_fns;

/* Opens libpipewire and resolves every function above. 0 on success; on
 * failure the reason is logged and nothing stays open. Callers serialize. */
int tpw_pw_dl_open(void);

/* Closes what tpw_pw_dl_open() opened. Nothing of libpipewire may be in use. */
void tpw_pw_dl_close(void);

#ifndef TPW_PW_DL_NO_REDIRECT
#define pw_context_add_spa_lib (tpw_pw_fns.pw_context_add_spa_lib)
#define pw_context_connect (tpw_pw_fns.pw_context_connect)
#define pw_context_destroy (tpw_pw_fns.pw_context_destroy)
#define pw_context_load_module (tpw_pw_fns.pw_context_load_module)
#define pw_context_new (tpw_pw_fns.pw_context_new)
#define pw_core_disconnect (tpw_pw_fns.pw_core_disconnect)
#define pw_deinit (tpw_pw_fns.pw_deinit)
#define pw_filter_add_listener (tpw_pw_fns.pw_filter_add_listener)
#define pw_filter_add_port (tpw_pw_fns.pw_filter_add_port)
#define pw_filter_connect (tpw_pw_fns.pw_filter_connect)
#define pw_filter_dequeue_buffer (tpw_pw_fns.pw_filter_dequeue_buffer)
#define pw_filter_destroy (tpw_pw_fns.pw_filter_destroy)
#define pw_filter_flush (tpw_pw_fns.pw_filter_flush)
#define pw_filter_get_node_id (tpw_pw_fns.pw_filter_get_node_id)
#define pw_filter_new (tpw_pw_fns.pw_filter_new)
#define pw_filter_queue_buffer (tpw_pw_fns.pw_filter_queue_buffer)
#define pw_filter_set_active (tpw_pw_fns.pw_filter_set_active)
#define pw_filter_update_params (tpw_pw_fns.pw_filter_update_params)
#define pw_filter_update_properties (tpw_pw_fns.pw_filter_update_properties)
#define pw_init (tpw_pw_fns.pw_init)
#define pw_properties_free (tpw_pw_fns.pw_properties_free)
#define pw_properties_new (tpw_pw_fns.pw_properties_new)
#define pw_properties_set (tpw_pw_fns.pw_properties_set)
#define pw_proxy_add_object_listener (tpw_pw_fns.pw_proxy_add_object_listener)
#define pw_proxy_destroy (tpw_pw_fns.pw_proxy_destroy)
#define pw_stream_add_listener (tpw_pw_fns.pw_stream_add_listener)
#define pw_stream_connect (tpw_pw_fns.pw_stream_connect)
#define pw_stream_dequeue_buffer (tpw_pw_fns.pw_stream_dequeue_buffer)
#define pw_stream_destroy (tpw_pw_fns.pw_stream_destroy)
#define pw_stream_flush (tpw_pw_fns.pw_stream_flush)
#define pw_stream_get_node_id (tpw_pw_fns.pw_stream_get_node_id)
#define pw_stream_get_time_n (tpw_pw_fns.pw_stream_get_time_n)
#define pw_stream_new (tpw_pw_fns.pw_stream_new)
#define pw_stream_queue_buffer (tpw_pw_fns.pw_stream_queue_buffer)
#define pw_stream_set_active (tpw_pw_fns.pw_stream_set_active)
#define pw_stream_update_params (tpw_pw_fns.pw_stream_update_params)
#define pw_thread_loop_destroy (tpw_pw_fns.pw_thread_loop_destroy)
#define pw_thread_loop_get_loop (tpw_pw_fns.pw_thread_loop_get_loop)
#define pw_thread_loop_get_time (tpw_pw_fns.pw_thread_loop_get_time)
#define pw_thread_loop_in_thread (tpw_pw_fns.pw_thread_loop_in_thread)
#define pw_thread_loop_lock (tpw_pw_fns.pw_thread_loop_lock)
#define pw_thread_loop_new (tpw_pw_fns.pw_thread_loop_new)
#define pw_thread_loop_signal (tpw_pw_fns.pw_thread_loop_signal)
#define pw_thread_loop_start (tpw_pw_fns.pw_thread_loop_start)
#define pw_thread_loop_stop (tpw_pw_fns.pw_thread_loop_stop)
#define pw_thread_loop_timed_wait_full (tpw_pw_fns.pw_thread_loop_timed_wait_full)
#define pw_thread_loop_unlock (tpw_pw_fns.pw_thread_loop_unlock)
#endif

#endif /* TPW_PW_DL_H */
