/* SPDX-License-Identifier: MIT */

/* Links only the public library, never libpipewire itself, so what this
 * process maps of libpipewire is exactly what the library loaded. */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tpw/tpw_filter.h"
#include "tpw/tpw_stream.h"
#include "tpw_test.h"

static void on_data(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void on_process(tpw_filter_h filter, tpw_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

static bool libpipewire_mapped(void)
{
    FILE* f = fopen("/proc/self/maps", "r");
    TPW_ASSERT(f != NULL);

    char line[512];
    bool mapped = false;
    while (fgets(line, sizeof(line), f))
        mapped = mapped || strstr(line, "/libpipewire-0.3.so") != NULL;
    fclose(f);
    return mapped;
}

int main(void)
{
    /* Nothing of PipeWire is loaded until a stream or filter needs it. */
    TPW_ASSERT(!libpipewire_mapped());

    for (int round = 0; round < 2; round++) {
        tpw_stream_h stream = tpw_stream_create(TPW_DATA_AUDIO, on_data, NULL);
        TPW_ASSERT(stream != NULL);
        TPW_ASSERT(libpipewire_mapped());

        /* A filter shares the load with the stream: destroying one of them
         * leaves libpipewire in place for the other. */
        tpw_filter_h filter = tpw_filter_create("tpw-test-unload", on_process, NULL);
        TPW_ASSERT(filter != NULL);
        tpw_stream_destroy(stream);
        TPW_ASSERT(libpipewire_mapped());

        /* The last one out unloads it, and the next round loads it again. */
        tpw_filter_destroy(filter);
        TPW_ASSERT(!libpipewire_mapped());
    }

    return 0;
}
