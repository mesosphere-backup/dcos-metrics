#pragma once

#include <string>
#include <vector>

namespace stats {
  /**
   * Utilities for adding key prefix tags to StatsD data.
   */
  class tag_key_prefix {
   public:

    /**
     * Prepends a single tag to the provided buffer.
     * Returns whether the tag was successfully added (false => buffer is too small).
     */
    static bool prepend_key(std::vector<char>& buffer, size_t& size, const std::string& tag);

    /**
     * Prepends multiple tags to the provided buffer.
     * Returns whether the tag was successfully added (false => buffer is too small).
     */
    static bool prepend_three_keys(std::vector<char>& buffer, size_t& size,
        const std::string& tag1, const std::string& tag2, const std::string& tag3);

   private:
    /**
     * No instantiation allowed.
     */
    tag_key_prefix() { }
    tag_key_prefix(const tag_key_prefix&) { }
  };
}
