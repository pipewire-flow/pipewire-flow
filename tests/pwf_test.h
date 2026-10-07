/* SPDX-License-Identifier: MIT */

#ifndef PWF_TEST_H
#define PWF_TEST_H

#include <stdio.h>
#include <stdlib.h>

/* Minimal in-repo test harness — no external test framework dependency. */
#define PWF_ASSERT(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

#define PWF_ASSERT_EQ(a, b) PWF_ASSERT((a) == (b))

#endif /* PWF_TEST_H */
