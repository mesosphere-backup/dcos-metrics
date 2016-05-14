#pragma once

#include <mesos/mesos.pb.h>

#include "mesos_hash.hpp"
#include "metrics_schema_struct.hpp"

namespace metrics {
  class AvroEncoder {
   public:
    /**
     * Returns a statically allocated header buffer.
     */
    static const std::string& header();

    /**
     * malloc()'s 'out', fills it with encoded metrics, and returns the size of the encoded metrics
     * inside of 'out'.
     */
    static void encode_metrics_block(
        const container_id_ord_map<metrics_schema::MetricList>& metric_map,
        std::ostream& ostream);

    /**
     * Returns the number of Datapoints added to the provided map of MetricLists
     */
    static size_t statsd_to_struct(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* data, size_t size,
        container_id_ord_map<metrics_schema::MetricList>& metric_map);

    /**
     * Returns the number of Datapoints added to the provided MetricLists
     */
    static size_t resources_to_struct(const mesos::ResourceUsage& usage,
        container_id_ord_map<metrics_schema::MetricList>& metric_map);

    /**
     * Returns whether the provided MetricList has nothing in it.
     */
    static bool empty(const metrics_schema::MetricList& metric_list);

   private:
    AvroEncoder() { /* do not instantiate */ }
  };
}
