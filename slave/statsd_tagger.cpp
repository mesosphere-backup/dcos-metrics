#include "statsd_tagger.hpp"

namespace {
  /**
   * Tags to use when there's a container issue.
   */
  const std::string UNKNOWN_CONTAINER_TAG("unknown_container"); // no container info provided

  /**
   * Tag names to use for datadog tags
   */
  const std::string CONTAINER_ID_DATADOG_KEY("container_id");
  const std::string EXECUTOR_ID_DATADOG_KEY("executor_id");
  const std::string FRAMEWORK_ID_DATADOG_KEY("framework_id");
}

// ---

size_t stats::NullTagger::calculate_size(
    const mesos::ContainerID* /*container_id*/, const mesos::ExecutorInfo* /*executor_info*/,
    const char* /*in_data*/, size_t in_size) {
  return in_size;
}

void stats::NullTagger::tag_copy(
    const mesos::ContainerID* /*container_id*/, const mesos::ExecutorInfo* /*executor_info*/,
    const char* in_data, size_t in_size, char* out_data) {
  memcpy(out_data, in_data, in_size);
}

// ---

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

size_t stats::KeyPrefixTagger::calculate_size(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* /*in_data*/, size_t in_size) {
  if (container_id == NULL || executor_info == NULL) {
    // unknown.in_data
    return UNKNOWN_CONTAINER_TAG.size() + 1 + in_size;
  } else {
    // fid.eid.cid.in_data
    return executor_info->framework_id().value().size() + 1
      + executor_info->executor_id().value().size() + 1
      + container_id->value().size() + 1
      + in_size;
  }
}

void stats::KeyPrefixTagger::tag_copy(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* in_data, size_t in_size, char* out_data) {
  size_t out_offset = 0, elem_size = 0;
  if (container_id == NULL || executor_info == NULL) {
    // unknown.in_data
    out_offset = UNKNOWN_CONTAINER_TAG.size();
    memcpy(out_data, UNKNOWN_CONTAINER_TAG.data(), out_offset);
    out_data[out_offset] = KEY_PREFIX_DELIMITER;
    ++out_offset;
    memcpy(out_data + out_offset, in_data, in_size);
  } else {
    // fid.eid.cid.in_data (with .'s within ids converted to _'s)

    // fid.
    elem_size = executor_info->framework_id().value().size();
    memcpy(out_data, executor_info->framework_id().value().data(), elem_size);
    replace_all(out_data + out_offset, elem_size,
        KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);
    out_offset += elem_size;
    out_data[out_offset] = KEY_PREFIX_DELIMITER;
    ++out_offset;

    // eid.
    elem_size = executor_info->executor_id().value().size();
    memcpy(out_data + out_offset, executor_info->executor_id().value().data(), elem_size);
    replace_all(out_data + out_offset, elem_size,
        KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);
    out_offset += elem_size;
    out_data[out_offset] = KEY_PREFIX_DELIMITER;
    ++out_offset;

    // cid.
    elem_size = container_id->value().size();
    memcpy(out_data + out_offset, container_id->value().data(), elem_size);
    replace_all(out_data + out_offset, elem_size,
        KEY_PREFIX_DELIMITER, KEY_PREFIX_DELIMITER_REPLACEMENT);
    out_offset += elem_size;
    out_data[out_offset] = KEY_PREFIX_DELIMITER;
    ++out_offset;

    // in_data
    memcpy(out_data + out_offset, in_data, in_size);
  }
}

// ---

namespace {
  const std::string DATADOG_TAG_PREFIX("|#");
  const std::string DATADOG_TAG_DIVIDER(",");
  const std::string DATADOG_TAG_KEY_VALUE_SEPARATOR(":");

  /**
   * Finds the first 'needle' in 'haystack', with explicit buffer sizes for each (\0 ignored).
   * Returns a pointer to the location of 'needle' within 'haystack', or NULL if no match was found.
   */
  const char* memnmem_imp(const char* haystack, size_t haystack_size,
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
}

stats::DatadogTagger::DatadogTagger()
  : tag_mode(TagMode::NONE),
    tag_insert_index(0) { }

size_t stats::DatadogTagger::calculate_size(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* in_data, size_t in_size) {
  calculate_tag_section(in_data, in_size);

  if (container_id == NULL || executor_info == NULL) {
    switch (tag_mode) {
      case TagMode::FIRST_TAG:
        // data|#unknown
        return in_size + DATADOG_TAG_PREFIX.size() + UNKNOWN_CONTAINER_TAG.size();

      case TagMode::APPEND_TAG_NO_DELIM:
        // dataunknown
        return in_size + UNKNOWN_CONTAINER_TAG.size();

      case TagMode::APPEND_TAG:
        // data,unknown
        return in_size + DATADOG_TAG_DIVIDER.size() + UNKNOWN_CONTAINER_TAG.size();

      case TagMode::NONE:
        return 0;// shouldn't happen
    }
  }

  // lets assume that the compiler optimizes this a bunch:
  size_t common_suffix_len =
    FRAMEWORK_ID_DATADOG_KEY.size() // fid
    + DATADOG_TAG_KEY_VALUE_SEPARATOR.size() + executor_info->framework_id().value().size() // :<fid>

    + DATADOG_TAG_DIVIDER.size() + EXECUTOR_ID_DATADOG_KEY.size() // ,eid
    + DATADOG_TAG_KEY_VALUE_SEPARATOR.size() + executor_info->executor_id().value().size() // :<eid>

    + DATADOG_TAG_DIVIDER.size() + CONTAINER_ID_DATADOG_KEY.size() // ,cid
    + DATADOG_TAG_KEY_VALUE_SEPARATOR.size() + container_id->value().size(); // :<cid>
  switch (tag_mode) {
    case TagMode::FIRST_TAG:
      // data|#[fid:<fid>,eid:<eid>,cid:<cid>]
      return in_size + DATADOG_TAG_PREFIX.size() + common_suffix_len;

    case TagMode::APPEND_TAG_NO_DELIM:
      // data[fid:<fid>,eid:<eid>,cid:<cid>]
      return in_size + common_suffix_len;

    case TagMode::APPEND_TAG:
      // data,[fid:<fid>,eid:<eid>,cid:<cid>]
      return in_size + DATADOG_TAG_DIVIDER.size() + common_suffix_len; // :<cid>

    case TagMode::NONE:
      return 0;// shouldn't happen
  }
  return 0;// happy compilers
}

void stats::DatadogTagger::tag_copy(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* in_data, size_t in_size, char* out_data) {
  if (tag_mode == TagMode::NONE) {
    // calculate_size wasn't called first!
    return;
  }

  // reuses the results of calling calculate_tag_section via calculate_size

  // copy [0,tag_insert_index)
  memcpy(out_data, in_data, tag_insert_index);

  // insert tags at tag_insert_index
  size_t inserted_size = 0;
  if (container_id == NULL || executor_info == NULL) {
    inserted_size += append_tag(out_data + tag_insert_index + inserted_size, UNKNOWN_CONTAINER_TAG);
  } else {
    inserted_size += append_tag(out_data + tag_insert_index + inserted_size,
        FRAMEWORK_ID_DATADOG_KEY, executor_info->framework_id().value());
    inserted_size += append_tag(out_data + tag_insert_index + inserted_size,
        EXECUTOR_ID_DATADOG_KEY, executor_info->executor_id().value());
    inserted_size += append_tag(out_data + tag_insert_index + inserted_size,
        CONTAINER_ID_DATADOG_KEY, container_id->value());
  }

  // copy [tag_insert_index,in_size)
  memcpy(out_data + tag_insert_index + inserted_size, in_data + tag_insert_index,
      in_size - tag_insert_index);

  tag_mode = TagMode::NONE;
  tag_insert_index = 0;
}


void stats::DatadogTagger::calculate_tag_section(const char* in_data, const size_t in_size) {
  const char* tag_section_ptr = memnmem_imp(in_data, in_size,
      DATADOG_TAG_PREFIX.data(), DATADOG_TAG_PREFIX.size());
  if (tag_section_ptr == NULL) {
    // No pre-existing tag section was found. Append tags in a new section:
    // data|#unknown or data|#fid:<fid>,eid:<eid>,cid:<cid>
    tag_mode = TagMode::FIRST_TAG;
    tag_insert_index = in_size;
    return;
  }

  // Data already has a tag section. Figure out how to append into that section.
  // Find the end of the tag section, which is either end of string or start of next section:
  size_t tag_section_start = tag_section_ptr - in_data;
  char* next_section_ptr =
    (char*) memchr(tag_section_ptr + 1, '|', in_size - tag_section_start - 1);

  char last_char_in_tag_section;
  if (next_section_ptr == NULL) {
    // The tag section goes to the end of the string, no other sections follow.
    // eg data|@0.5|#tag:val,tag2:val2
    last_char_in_tag_section = in_data[in_size - 1];
    tag_insert_index = in_size;
  } else {
    // The tag section is NOT at the tail end of the string, there's other stuff after it
    // eg data|#tag:val,tag2:val2|@0.5
    last_char_in_tag_section = *(next_section_ptr - 1);
    tag_insert_index = next_section_ptr - in_data;
  }

  // Now, check the end of the tag section to see whether we will be adding a comma:
  switch (last_char_in_tag_section) {
    case ',': // data|#tag:val,tag2:val2, <-- don't add an additional comma
    case '#': // data|# <-- don't add an additional comma
      // Rare case: Tag section is empty or has a dangling comma. We should omit our comma.
      // fid:<fid>,eid:<eid>,cid:<cid>
      tag_mode = TagMode::APPEND_TAG_NO_DELIM;
      break;
    default:
      // Typical case: Add tag with a preceding delimiter
      // ,fid:<fid>,eid:<eid>,cid:<cid>
      tag_mode = TagMode::APPEND_TAG;
      break;
  }
}


size_t stats::DatadogTagger::append_tag(char* out_data, const std::string& tag) {
  size_t added = 0;
  switch (tag_mode) {
    case TagMode::FIRST_TAG:
      // <buffer>|#tag
      memcpy(out_data, DATADOG_TAG_PREFIX.data(), DATADOG_TAG_PREFIX.size());
      added = DATADOG_TAG_PREFIX.size();
      tag_mode = APPEND_TAG;
      break;
    case TagMode::APPEND_TAG:
      // <buffer>,tag
      memcpy(out_data, DATADOG_TAG_DIVIDER.data(), DATADOG_TAG_DIVIDER.size());
      added = DATADOG_TAG_DIVIDER.size();
      break;
    case TagMode::APPEND_TAG_NO_DELIM:
      // <buffer>tag
      tag_mode = APPEND_TAG;
      break;
    case TagMode::NONE:
      return 0;// shouldn't happen
  }

  memcpy(out_data + added, tag.data(), tag.size());
  added += tag.size();

  return added;
}

size_t stats::DatadogTagger::append_tag(
    char* out_data, const std::string& tag_key, const std::string& tag_value) {
  size_t added = 0;
  switch (tag_mode) {
    case TagMode::FIRST_TAG:
      // <buffer>|#key:value
      memcpy(out_data, DATADOG_TAG_PREFIX.data(), DATADOG_TAG_PREFIX.size());
      added = DATADOG_TAG_PREFIX.size();
      tag_mode = APPEND_TAG;
      break;
    case TagMode::APPEND_TAG:
      // <buffer>,key:value
      memcpy(out_data, DATADOG_TAG_DIVIDER.data(), DATADOG_TAG_DIVIDER.size());
      added = DATADOG_TAG_DIVIDER.size();
      break;
    case TagMode::APPEND_TAG_NO_DELIM:
      // <buffer>key:value
      tag_mode = APPEND_TAG;
      break;
    case TagMode::NONE:
      return 0;// shouldn't happen
  }

  memcpy(out_data + added, tag_key.data(), tag_key.size());
  added += tag_key.size();

  memcpy(out_data + added,
      DATADOG_TAG_KEY_VALUE_SEPARATOR.data(), DATADOG_TAG_KEY_VALUE_SEPARATOR.size());
  added += DATADOG_TAG_KEY_VALUE_SEPARATOR.size();

  memcpy(out_data + added, tag_value.data(), tag_value.size());
  added += tag_value.size();

  return added;
}
