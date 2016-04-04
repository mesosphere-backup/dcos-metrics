#pragma once

#include <string>
#include <vector>

namespace stats {
  /**
   * Utilities for adding Datadog-formatted tags to StatsD data.
   */
  class tag_datadog {
   public:
    enum TagMode {
      FIRST_TAG,
      APPEND_TAG_NO_DELIM,
      APPEND_TAG
    };

    /**
     * Updates the provided buffer in-place for appending tag data.
     * Returns the mode to use when appending tags to this buffer.
     */
    static TagMode prepare_for_tags(
        char* buffer, size_t size, std::vector<char>& scratch_buffer);

    /**
     * Appends a tag to the end of the provided buffer, with a delimiter determined by "tag_mode".
     * Returns whether the tag was successfully added (false => buffer is too small), and updates
     * "tag_mode" to the mode to be used in any next append_tag() call.
     */
    static bool append_tag(std::vector<char>& buffer, size_t& size,
        const std::string& tag, TagMode& tag_mode);

    /**
     * Appends a tag to the end of the provided buffer, with a delimiter determined by "tag_mode".
     * Returns whether the tag was successfully added (false => buffer is too small), and updates
     * "tag_mode" to the mode to be used in any next append_tag() call.
     */
    static bool append_tag(std::vector<char>& buffer, size_t& size,
        const std::string& tag_key, const std::string& tag_value, TagMode& tag_mode);

   private:
    /**
     * No instantiation allowed.
     */
    tag_datadog() { }
    tag_datadog(const tag_datadog&) { }
  };
}
