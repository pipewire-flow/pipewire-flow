/* SPDX-License-Identifier: MIT */

#ifndef PWF_TEST_HW_DISCOVER_H
#define PWF_TEST_HW_DISCOVER_H

/* Finds a real capture device so a hardware test never hardcodes a node
 * name. Kept out of the library: picking a device is a test's job. */

/* Copies the name of the first node of class `media_class` (for example
 * "Video/Source") into `out`. Returns false when the graph has none, in
 * which case the caller should skip the test. */
bool pwf_test_find_node(const char* media_class, char* out, size_t out_size);

#endif /* PWF_TEST_HW_DISCOVER_H */
