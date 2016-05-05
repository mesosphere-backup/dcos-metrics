#pragma once

#include <string>
#include <vector>
#include <mesos/mesos.pb.h>

namespace metrics {
  class StatsdTagger {
   public:
    /**
     * Returns the required size for a tagged version of the provided data.
     */
    virtual size_t calculate_size(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, const size_t in_size) = 0;
    /**
     * Copies a tagged version of the provided data into 'out_data', which MUST be at least the size
     * returned by a previous call to calculate_size().
     */
    virtual void tag_copy(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, size_t in_size, char* out_data) = 0;
  };

  class NullTagger : public StatsdTagger {
   public:
    size_t calculate_size(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, const size_t in_size);
    void tag_copy(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, size_t in_size, char* out_data);
  };

  class KeyPrefixTagger : public StatsdTagger {
   public:
    size_t calculate_size(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, const size_t in_size);
    void tag_copy(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, size_t in_size, char* out_data);
  };

  class DatadogTagger : public StatsdTagger {
   public:
    DatadogTagger();

    size_t calculate_size(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, const size_t in_size);
    void tag_copy(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* in_data, size_t in_size, char* out_data);

   private:
    enum TagMode {
      NONE,
      FIRST_TAG,
      APPEND_TAG_NO_DELIM,
      APPEND_TAG
    };

    void calculate_tag_section(const char* in_data, const size_t in_size);
    size_t append_tag(char* out_data, const std::string& tag);
    size_t append_tag(char* out_data, const std::string& tag_key, const std::string& tag_value);

    TagMode tag_mode;
    size_t tag_insert_index;
  };
}
