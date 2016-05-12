#pragma once

#include <stddef.h>

/**
 * Finds the first 'needle' in 'haystack', with explicit buffer sizes for each (\0 ignored).
 * Returns a pointer to the location of 'needle' within 'haystack', or NULL if no match was found.
 */
extern const char* memnmem(const char* haystack, size_t haystack_size,
    const char* needle, size_t needle_size);
