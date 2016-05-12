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
    static std::string header();

    /**
     * malloc()'s 'out', fills it with encoded metrics, and returns the size of the encoded metrics
     * inside of 'out'.
     */
    static void encode_metrics(
        const container_id_map<metrics_schema::MetricList>& metric_map,
        const metrics_schema::MetricList& metric_list,
        std::ostream& ostream);

    /**
     * Returns the number of Datapoints added to the provided map of MetricLists
     */
    static size_t statsd_to_struct(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* data, size_t size,
        container_id_map<metrics_schema::MetricList>& metric_map);

    /**
     * Returns the number of Datapoints added to the provided MetricList
     */
    static size_t statsd_to_struct(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* data, size_t size,
        metrics_schema::MetricList& metric_list);

    /**
     * Returns the number of Datapoints added to the provided MetricList
     */
    static size_t resources_to_struct(const mesos::ResourceUsage& usage,
        metrics_schema::MetricList& metric_list);

   private:
    AvroEncoder() { /* do not instantiate */ }
  };
}
