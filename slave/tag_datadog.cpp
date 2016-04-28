#include "tag_datadog.hpp"

#include <string.h>

#include <glog/logging.h>

namespace {
  const std::string DATADOG_TAG_PREFIX("|#");
  const std::string DATADOG_TAG_DIVIDER(",");
  const std::string DATADOG_TAG_KEY_VALUE_SEPARATOR(":");

  /**
   * Finds the first 'needle' in 'haystack', with explicit buffer sizes for each (\0 ignored).
   * Returns a pointer to the location of 'needle' within 'haystack', or NULL if no match was found.
   */
  char* memnmem_imp(char* haystack, size_t haystack_size, const char* needle, size_t needle_size) {
      if (haystack_size == 0 || needle_size == 0) {
      return NULL;
    }
    size_t search_start = 0;
    char* haystack_end = haystack + haystack_size;
    for (;;) {
      // Search for the first character in needle
      char* candidate =
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
}

stats::tag_datadog::TagMode stats::tag_datadog::prepare_for_tags(
    char* buffer, size_t size, std::vector<char>& scratch_buffer) {
  char* tag_section_ptr = memnmem_imp(buffer, size,
      DATADOG_TAG_PREFIX.data(), DATADOG_TAG_PREFIX.size());
  if (tag_section_ptr == NULL) {
    // No pre-existing tag section was found. Append tags in a new section.
    return TagMode::FIRST_TAG;
  }

  // Ensure the preexisting tag section is placed at the end of the buffer, so that we can append.
  // For example "io.mesosphere.sent.bytes=0|g|#tag1=val1,tag2=val2|@0.1"
  //          => "io.mesosphere.sent.bytes=0|g|@0.1|#tag1:val1,tag2:val2"
  // If there were multiple tag sections in a single record, this only moves the first one, but
  // if someone's generating multiple tag sections (probably a bug) then they shouldn't care that
  // we aren't merging all of them anyway.
  size_t before_tag_section_size = tag_section_ptr - buffer;
  char* after_tag_section_ptr =
    (char*) memchr(tag_section_ptr + 1, '|', size - before_tag_section_size - 1);
  if (after_tag_section_ptr != NULL) {
    // The tag section we found is followed by one or more other sections.
    // Move the tag section to the end of the buffer, swapping it with any other data:
    // "|#tag1,tag2:value2|@0.5|&amp" => "|@0.5|&amp|#tag1,tag2:value2"

    // Copy the tag section into a separate scratch buffer, which is grown on demand.
    size_t tag_section_size = after_tag_section_ptr - tag_section_ptr;
    if (scratch_buffer.size() < tag_section_size) {
      DLOG(INFO) << "Grow scratch buffer for tag order to " << tag_section_size << " bytes";
      scratch_buffer.resize(tag_section_size, '\0');
    }
    memcpy(scratch_buffer.data(), tag_section_ptr, tag_section_size);

    // Move forward the data following the tag section
    int64_t after_tag_section_size = size - before_tag_section_size - tag_section_size;
    if (after_tag_section_size > (after_tag_section_ptr - tag_section_ptr)) {
      // memmove required: Copying between ranges which overlap within the same buffer.
      memmove(tag_section_ptr, after_tag_section_ptr, after_tag_section_size);
    } else {
      memcpy(tag_section_ptr, after_tag_section_ptr, after_tag_section_size);
    }

    // Copy back the separate buffer containing the tag data, following the data we moved forward.
    memcpy(tag_section_ptr + after_tag_section_size, scratch_buffer.data(), tag_section_size);
  }

  // Tag section is now at the end of the buffer, ready for append.
  switch (buffer[size - 1]) {
    case ',':
    case '#':
      // Corner case: Empty tag data. Clean this up by omitting the delimiter when we append.
      return TagMode::APPEND_TAG_NO_DELIM;
    default:
      // Typical case: Add tag with a preceding delimiter
      return TagMode::APPEND_TAG;
  }
}

bool stats::tag_datadog::append_tag(std::vector<char>& buffer, size_t& size,
    const std::string& tag, TagMode& tag_mode) {
  switch (tag_mode) {
    case FIRST_TAG:
      // <buffer>|#tag
      if (size + DATADOG_TAG_PREFIX.size() + tag.size() > buffer.size()) {
        return false;
      }
      memcpy(buffer.data() + size, DATADOG_TAG_PREFIX.data(), DATADOG_TAG_PREFIX.size());
      size += DATADOG_TAG_PREFIX.size();
      tag_mode = APPEND_TAG;
      break;
    case APPEND_TAG:
      // <buffer>,tag
      if (size + DATADOG_TAG_DIVIDER.size() + tag.size() > buffer.size()) {
        return false;
      }
      memcpy(buffer.data() + size, DATADOG_TAG_DIVIDER.data(), DATADOG_TAG_DIVIDER.size());
      size += DATADOG_TAG_DIVIDER.size();
      break;
    case APPEND_TAG_NO_DELIM:
      // <buffer>tag
      if (size + tag.size() > buffer.size()) {
        return false;
      }
      tag_mode = APPEND_TAG;
      break;
  }
  memcpy(buffer.data() + size, tag.data(), tag.size());
  size += tag.size();
  return true;
}

bool stats::tag_datadog::append_tag(std::vector<char>& buffer, size_t& size,
    const std::string& tag_key, const std::string& tag_value, TagMode& tag_mode) {
  switch (tag_mode) {
    case FIRST_TAG:
      // <buffer>|#key:value
      if ((size
              + DATADOG_TAG_PREFIX.size()
              + tag_key.size()
              + DATADOG_TAG_KEY_VALUE_SEPARATOR.size()
              + tag_value.size())
          > buffer.size()) {
        return false;
      }
      memcpy(buffer.data() + size, DATADOG_TAG_PREFIX.data(), DATADOG_TAG_PREFIX.size());
      size += DATADOG_TAG_PREFIX.size();
      tag_mode = APPEND_TAG;
      break;
    case APPEND_TAG:
      // <buffer>,key:value
      if ((size
              + DATADOG_TAG_DIVIDER.size()
              + tag_key.size()
              + DATADOG_TAG_KEY_VALUE_SEPARATOR.size()
              + tag_value.size())
          > buffer.size()) {
        return false;
      }
      memcpy(buffer.data() + size, DATADOG_TAG_DIVIDER.data(), DATADOG_TAG_DIVIDER.size());
      size += DATADOG_TAG_DIVIDER.size();
      break;
    case APPEND_TAG_NO_DELIM:
      // <buffer>key:value
      if ((size
              + tag_key.size()
              + DATADOG_TAG_KEY_VALUE_SEPARATOR.size()
              + tag_value.size())
          > buffer.size()) {
        return false;
      }
      tag_mode = APPEND_TAG;
      break;
  }
  memcpy(buffer.data() + size, tag_key.data(), tag_key.size());
  size += tag_key.size();
  memcpy(buffer.data() + size,
      DATADOG_TAG_KEY_VALUE_SEPARATOR.data(), DATADOG_TAG_KEY_VALUE_SEPARATOR.size());
  size += DATADOG_TAG_KEY_VALUE_SEPARATOR.size();
  memcpy(buffer.data() + size, tag_value.data(), tag_value.size());
  size += tag_value.size();
  return true;
}
