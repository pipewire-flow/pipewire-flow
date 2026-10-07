/* SPDX-License-Identifier: MIT */

#include <time.h>

#include "pwf_stream_internal.h"

/* A condition that repeats every cycle would otherwise log every cycle,
 * turning a problem on the real-time thread into a much worse one. */
#define PWF_LOG_INTERVAL_NS 1000000000ull

uint64_t pwf_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* A zero timestamp means nothing has been logged yet, so the first
 * occurrence of a run always reports. */
bool pwf_rate_limited(uint64_t* last_log_ns, uint64_t* suppressed, uint64_t now_ns)
{
    if (*last_log_ns != 0 && now_ns - *last_log_ns < PWF_LOG_INTERVAL_NS) {
        (*suppressed)++;
        return false;
    }

    *last_log_ns = now_ns;
    return true;
}
