/* SPDX-License-Identifier: MIT */

/**
 * @file pwf_export.h
 * @brief Marks the functions that make up the shared library's public ABI.
 */

#ifndef PWF_EXPORT_H
#define PWF_EXPORT_H

/* The library is built with hidden visibility, so only declarations carrying
 * PWF_API are exported. Defining PWF_API first overrides it. */
#ifndef PWF_API
#if defined(__GNUC__) && __GNUC__ >= 4
#define PWF_API __attribute__((visibility("default")))
#else
#define PWF_API
#endif
#endif

#endif /* PWF_EXPORT_H */
