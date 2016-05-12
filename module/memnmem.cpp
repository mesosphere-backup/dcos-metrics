#include "memnmem.h"

#include <string.h>

/**
 * Finds the first 'needle' in 'haystack', with explicit buffer sizes for each (\0 ignored).
 * Returns a pointer to the location of 'needle' within 'haystack', or NULL if no match was found.
 */
const char* memnmem(const char* haystack, size_t haystack_size,
    const char* needle, size_t needle_size) {
  if (haystack_size == 0 || needle_size == 0) {
    return NULL;
  }
  size_t search_start = 0;
  const char* haystack_end = haystack + haystack_size;
  for (;;) {
    // Search for the first character in needle
    const char* candidate =
      (char*) memchr(haystack + search_start, needle[0], haystack_size - search_start);
    if (candidate == NULL) {
      return NULL;
    }
    if (candidate + needle_size > haystack_end) {
      return NULL;
    }
    // Check whether the following characters also match
    if (memcmp(&candidate[1], &needle[1], needle_size - 1) == 0) {
      return candidate;
    } else {
      search_start = candidate - haystack + 1;
    }
  }
}
