/* SPDX-License-Identifier: MIT */

#include "pwf/pwf_filter.h"
#include "pwf_filter_internal.h" /* whitebox: the ns->ratio conversion */
#include "pwf_test.h"

static void noop_process_cb(pwf_filter_h filter, pwf_filter_port_buffer* buffers, size_t n_buffers,
                             void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)n_buffers;
    (void)user_data;
}

/* The hint is a duration expressed as "num/48000"; whole milliseconds land
 * exactly (num == ms * 48), sub-tick values floor and clamp to >= 1. */
static void test_period_conversion(void)
{
    PWF_ASSERT_EQ(pwf_filter_period_hint_num(10000000), 480u); /* 10 ms */
    PWF_ASSERT_EQ(pwf_filter_period_hint_num(5000000), 240u);  /* 5 ms  */
    PWF_ASSERT_EQ(pwf_filter_period_hint_num(1000000), 48u);   /* 1 ms  */
    PWF_ASSERT_EQ(pwf_filter_period_hint_num(1000), 1u);       /* 1 us -> floored, clamped to 1 */
}

int main(void)
{
    test_period_conversion();

    pwf_filter_h filter = pwf_filter_create("pwf-test-period-hint", noop_process_cb, NULL);
    PWF_ASSERT(filter != NULL);

    pwf_filter_port_h in = pwf_filter_add_signal_port(filter, PWF_FILTER_PORT_INPUT);
    PWF_ASSERT(in != NULL);

    /* Accepted before start; 0 clears it. */
    PWF_ASSERT_EQ(pwf_filter_set_period_hint(filter, 10000000), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_set_period_hint(filter, 0), PWF_OK);
    PWF_ASSERT_EQ(pwf_filter_set_period_hint(filter, 10000000), PWF_OK);

    PWF_ASSERT_EQ(pwf_filter_start(filter), PWF_OK);

    /* Rejected after start (it is a connect-time node property). */
    PWF_ASSERT_EQ(pwf_filter_set_period_hint(filter, 5000000), PWF_ERR_INVALID_ARG);

    pwf_filter_stop(filter, false);
    pwf_filter_destroy(filter);
    return 0;
}
