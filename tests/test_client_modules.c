/* SPDX-License-Identifier: MIT */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tpw/tpw_stream.h"
#include "tpw_test.h"

static void on_data(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

/* Counts the distinct libpipewire-module-*.so files mapped into this
 * process, and reports whether `name` is among them. */
static int mapped_modules(const char* name, bool* found)
{
    FILE* f = fopen("/proc/self/maps", "r");
    TPW_ASSERT(f != NULL);

    char seen[32][128];
    int n = 0;
    char line[512];
    *found = false;
    while (fgets(line, sizeof(line), f)) {
        const char* mod = strstr(line, "libpipewire-module-");
        if (!mod)
            continue;
        char base[128];
        if (sscanf(mod, "%127[^.\n]", base) != 1)
            continue;
        bool dup = false;
        for (int i = 0; i < n; i++)
            dup = dup || strcmp(seen[i], base) == 0;
        if (dup || n == 32)
            continue;
        snprintf(seen[n++], sizeof(seen[0]), "%s", base);
        if (name && strcmp(base, name) == 0)
            *found = true;
    }
    fclose(f);
    return n;
}

static tpw_stream_h start_stream(void)
{
    tpw_stream_h s = tpw_stream_create(TPW_DATA_AUDIO, on_data, NULL);
    TPW_ASSERT(s != NULL);
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(s, &(tpw_audio_config){ .sample_rate = 48000, .channels = 2 }), TPW_OK);
    TPW_ASSERT_EQ(tpw_stream_start(s), TPW_OK);
    return s;
}

int main(void)
{
    bool found;

    /* By default a stream's context loads exactly the three modules a
     * client needs, none of the extras the stock client.conf lists. */
    unsetenv("PIPEWIRE_CONFIG_NAME");
    tpw_stream_h s = start_stream();
    TPW_ASSERT_EQ(mapped_modules("libpipewire-module-protocol-native", &found), 3);
    TPW_ASSERT(found);
    mapped_modules("libpipewire-module-client-node", &found);
    TPW_ASSERT(found);
    mapped_modules("libpipewire-module-adapter", &found);
    TPW_ASSERT(found);
    tpw_stream_stop(s, false);
    tpw_stream_destroy(s);

    /* A configuration the user names is used as is: client.conf brings in
     * more than the minimal set. The last destroy above unloaded the
     * modules, so what is mapped now comes from this stream alone. */
    TPW_ASSERT_EQ(mapped_modules(NULL, &found), 0);
    setenv("PIPEWIRE_CONFIG_NAME", "client.conf", 1);
    s = start_stream();
    TPW_ASSERT(mapped_modules(NULL, &found) > 3);
    tpw_stream_stop(s, false);
    tpw_stream_destroy(s);

    return 0;
}
