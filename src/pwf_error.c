/* SPDX-License-Identifier: MIT */

#include "pwf/pwf_types.h"

const char* pwf_strerror(int error)
{
    switch (error) {
    case PWF_OK:
        return "Success";
    case PWF_ERR_INVALID_ARG:
        return "Invalid argument";
    case PWF_ERR_CONNECT_FAILED:
        return "PipeWire connection or request failed";
    case PWF_ERR_INVALID_FORMAT:
        return "Unsupported or non-negotiable format";
    case PWF_ERR_NOT_CONFIGURED:
        return "A required earlier call is missing";
    case PWF_ERR_SOURCE_UNAVAILABLE:
        return "Source unavailable";
    case PWF_ERR_IN_CALLBACK:
        return "Not callable from inside a callback";
    case PWF_ERR_NOT_FOUND:
        return "Target not found";
    case PWF_ERR_TIMEOUT:
        return "Timed out";
    case PWF_ERR_NO_MEMORY:
        return "Out of memory";
    case PWF_ERR_INVALID_STATE:
        return "Call not allowed in the current state or routing mode";
    default:
        return "Unknown error";
    }
}
