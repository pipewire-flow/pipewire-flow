/* SPDX-License-Identifier: MIT */

/* Wires a capture stream to a device without a session manager deciding
 * anything. The link is deferred until you press Enter so the graph can be
 * inspected before and after:
 *
 *   pw-link -l | grep <device>     # nothing, then one line per channel
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pwf/pwf_stream.h"

static volatile sig_atomic_t g_running = 1;
static unsigned g_buffers;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void on_data(pwf_stream_h stream, const pwf_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
    g_buffers++;
}

static void on_error(pwf_stream_h stream, int error_code, void* user_data)
{
    (void)stream;
    (void)user_data;
    /* Nothing re-links us: that decision is ours to make. */
    fprintf(stderr, "device lost (error %d) — nothing will re-route on its own\n", error_code);
    g_running = 0;
}

static void wait_for_enter(const char* prompt)
{
    printf("%s", prompt);
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <device-node-name> [second-device-node-name]\n"
                        "  names come from `wpctl status`; an object.serial works too.\n",
                argv[0]);
        return 1;
    }

    signal(SIGINT, on_signal);

    pwf_stream_h s = pwf_stream_create(PWF_DATA_AUDIO, on_data, NULL);
    if (!s) {
        fprintf(stderr, "failed to create stream (is PipeWire running?)\n");
        return 1;
    }
    pwf_stream_set_error_cb(s, on_error);

    /* From here the session manager will not wire this stream. */
    if (pwf_stream_set_autoconnect(s, false) != PWF_OK) {
        fprintf(stderr, "failed to take over routing\n");
        pwf_stream_destroy(s);
        return 1;
    }

    pwf_audio_config cfg = { .sample_rate = 48000, .channels = 2, .format = "S16" };
    if (pwf_stream_set_audio_config(s, &cfg) != PWF_OK ||
        pwf_stream_start(s) != PWF_OK) {
        fprintf(stderr, "failed to configure or start the stream\n");
        pwf_stream_destroy(s);
        return 1;
    }

    printf("started, wired to nothing. check: pw-link -l | grep %s\n", argv[1]);
    wait_for_enter("press Enter to link... ");

    int res = pwf_stream_link(s, argv[1]);
    if (res != PWF_OK) {
        fprintf(stderr, "link failed (%d)\n", res);
        pwf_stream_destroy(s);
        return 1;
    }
    printf("linked to %s — check pw-link -l again\n", argv[1]);

    wait_for_enter("press Enter to stop and start (links must survive)... ");
    pwf_stream_stop(s, false);
    printf("stopped; links are still there\n");
    pwf_stream_start(s);
    printf("started again on the same device\n");

    if (argc > 2) {
        wait_for_enter("press Enter to re-target... ");
        pwf_stream_unlink(s);
        if (pwf_stream_link(s, argv[2]) == PWF_OK)
            printf("moved to %s\n", argv[2]);
        else
            fprintf(stderr, "could not move to %s\n", argv[2]);
    }

    printf("capturing, press Ctrl+C to stop...\n");
    while (g_running)
        sleep(1);

    printf("%u buffers received\n", g_buffers);
    pwf_stream_stop(s, false);
    pwf_stream_destroy(s);
    return 0;
}
