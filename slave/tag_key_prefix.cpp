#include "tag_key_prefix.hpp"

#include <string.h>

#include <glog/logging.h>

namespace {
  const char KEY_PREFIX_DELIMITER = '.';
  const char KEY_PREFIX_DELIMITER_REPLACEMENT = '_';

  inline void replace_all(char* buffer, size_t size_left,
      const char search, const char replace) {
    char* last_found = buffer;
    char* cur_found = NULL;
    while ((cur_found = (char*)memchr(last_found, search, size_left)) != NULL) {
      size_left -= cur_found - last_found;
      *cur_found = replace;
      last_found = cur_found;
    }
  }
}

bool stats::tag_key_prefix::prepend_key(
    std::vector<char>& buffer, size_t& size, const std::string& tag) {
  // tag_with_underscores.<buffer>
  size_t tag_size = sizeof(KEY_PREFIX_DELIMITER) + tag.size();
  if (size + tag_size > buffer.size()) {
    return false;
  }

  // shift buffer to fit tag + delim
  memmove(buffer.data() + tag_size, buffer.data(), size);
  // insert tag + delim
  memcpy(buffer.data(), tag.data(), tag.size());
  buffer.data()[tag.size()] = KEY_PREFIX_DELIMITER;
  // replace any .'s in tag with _'s
  replace_all(buffer.data(), tag.size(), KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);

  size += tag_size;
  return true;
}

bool stats::tag_key_prefix::prepend_three_keys(std::vector<char>& buffer, size_t& size,
    const std::string& tag1, const std::string& tag2, const std::string& tag3) {
  // tag1_with_underscores.tag2_with_underscores.tag3_with_underscores.<buffer>
  size_t tags_size = (3 * sizeof(KEY_PREFIX_DELIMITER))
    + (tag1.size() + tag2.size() + tag3.size());
  if (size + tags_size > buffer.size()) {
    return false;
  }

  // shift buffer to fit tags + delims
  memmove(buffer.data() + tags_size, buffer.data(), size);

  // insert tags + delims, replace any .'s in tags with _'s
  char* ptr = buffer.data();

  memcpy(ptr, tag1.data(), tag1.size());
  ptr[tag1.size()] = KEY_PREFIX_DELIMITER;
  replace_all(ptr, tag1.size(), KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);

  ptr += tag1.size() + sizeof(KEY_PREFIX_DELIMITER);

  memcpy(ptr, tag2.data(), tag2.size());
  ptr[tag2.size()] = KEY_PREFIX_DELIMITER;
  replace_all(ptr, tag2.size(), KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);

  ptr += tag2.size() + sizeof(KEY_PREFIX_DELIMITER);

  memcpy(ptr, tag3.data(), tag3.size());
  ptr[tag3.size()] = KEY_PREFIX_DELIMITER;
  replace_all(ptr, tag3.size(), KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);

  size += tags_size;
  return true;
}
