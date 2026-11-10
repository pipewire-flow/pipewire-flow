/* SPDX-License-Identifier: MIT */

/* pwf_strerror() names every enum pwf_error value with its own phrase and
 * never returns NULL, so it is safe to print whatever a call returned. */

#include <string.h>

#include "pwf/pwf_types.h"
#include "pwf_test.h"

int main(void)
{
    /* The codes are contiguous from PWF_OK down to the newest one; a code
     * added without a phrase would fall through to "Unknown error" here. */
    const char* seen[16];
    size_t n = 0;
    for (int code = PWF_OK; code >= PWF_ERR_NO_SPACE; code--) {
        const char* text = pwf_strerror(code);
        PWF_ASSERT(text != NULL);
        PWF_ASSERT(*text != '\0');
        PWF_ASSERT(strcmp(text, "Unknown error") != 0);
        for (size_t i = 0; i < n; i++)
            PWF_ASSERT(strcmp(text, seen[i]) != 0);
        seen[n++] = text;
    }

    PWF_ASSERT_EQ(strcmp(pwf_strerror(PWF_OK), "Success"), 0);

    /* Values outside the enum, on either side, still give a printable string. */
    PWF_ASSERT_EQ(strcmp(pwf_strerror(PWF_ERR_NO_SPACE - 1), "Unknown error"), 0);
    PWF_ASSERT_EQ(strcmp(pwf_strerror(1), "Unknown error"), 0);

    return 0;
}
