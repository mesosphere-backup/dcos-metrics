#pragma once

#include <mesos/mesos.pb.h>

#include "mesos_hash.hpp"
#include "metrics_schema_struct.hpp"

namespace metrics {
  /**
   * We allow containers to emit metrics which contain their own custom tags.
   * Tags are populated at the root of the MetricList, so we put each metric with custom tags into
   * a separate MetricList of its own. In theory we could merge two MetricLists when they contain
   * the same custom tag values, but that'd be a lot of work.
   *
   * In practice, most emitters don't include tags, so their datapoints all go into `without_tags`.
   */
  class ContainerMetrics {
   public:
    metrics_schema::MetricList without_custom_tags;
    std::vector<metrics_schema::MetricList> with_custom_tags;
  };
  typedef container_id_ord_map<ContainerMetrics> avro_metrics_map_t;

  class AvroEncoder {
   public:
    /**
     * Returns a statically allocated header buffer.
     */
    static const std::string& header();

    /**
     * Writes the provided metrics to the provided output stream.
     */
    static void encode_metrics_block(
        const avro_metrics_map_t& metric_map, std::ostream& ostream);

    /**
     * Returns the number of Datapoints added to the provided map of MetricLists
     */
    static size_t statsd_to_map(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* data, size_t size,
        avro_metrics_map_t& metric_map);

    /**
     * Returns whether the provided MetricList has nothing in it.
     */
    static bool empty(const metrics_schema::MetricList& metric_list);

   private:
    AvroEncoder() { /* do not instantiate */ }
  };
}
