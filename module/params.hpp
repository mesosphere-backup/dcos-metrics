#pragma once

#include <stddef.h>

#include <mesos/mesos.pb.h>

namespace metrics {
  namespace params {
    /**
     * Types
     */

    namespace port_mode {
      enum Value { UNKNOWN, SINGLE, EPHEMERAL, RANGE };
    }
    port_mode::Value to_port_mode(const std::string& param);

    namespace annotation_mode {
      enum Value { UNKNOWN, NONE, TAG_DATADOG, KEY_PREFIX };
    }
    annotation_mode::Value to_annotation_mode(const std::string& param);

    /**
     * Container input settings
     */

    // The host to listen on. Should stay with "localhost" except in ip-per-container environments.
    const std::string LISTEN_HOST = "listen_host";
    const std::string LISTEN_HOST_DEFAULT = "127.0.0.1";

    // The mode to use for assigning containers to listen ports.
    const std::string LISTEN_PORT_MODE = "listen_port_mode";

    // Uses a single port for all containers on the agent. Only advisable in ip-per-container environments.
    // In this mode, listen_port must be non-zero.
    const std::string LISTEN_PORT_MODE_SINGLE = "single";
    const std::string LISTEN_PORT = "listen_port";
    const size_t LISTEN_PORT_DEFAULT = 0;

    // Listens to ports in the OS-defined ephemeral port range which are then dynamically assigned to containers.
    // See /proc/sys/net/ipv4/ip_local_port_range and/or sysctl's net.ipv4.ip_local_port_range.
    const std::string LISTEN_PORT_MODE_EPHEMERAL = "ephemeral";

    // Listens to a specified range of ports which are then dynamically assigned to containers.
    // In this mode, listen_port_start/listen_port_end must be non-zero.
    const std::string LISTEN_PORT_MODE_RANGE = "range";
    const std::string LISTEN_PORT_START = "listen_port_start";
    const size_t LISTEN_PORT_START_DEFAULT = 0;
    const std::string LISTEN_PORT_END = "listen_port_end";
    const size_t LISTEN_PORT_END_DEFAULT = 0;

    // Default to ephemeral unless/until ip-per-container becomes common.
    const std::string LISTEN_PORT_MODE_DEFAULT = LISTEN_PORT_MODE_EPHEMERAL;

    /**
     * Collector output settings
     */

    // Whether export to a Collector process is enabled.
    const std::string OUTPUT_COLLECTOR_ENABLED = "output_collector_enabled";
    const bool OUTPUT_COLLECTOR_ENABLED_DEFAULT = true;

    const std::string OUTPUT_COLLECTOR_HOST = "output_collector_host";
    const std::string OUTPUT_COLLECTOR_HOST_DEFAULT = "127.0.0.1";

    // The period in seconds between host resolutions. Automatically detects changes in DNS records,
    // with automatic selection of a random A record if multiple entries are configured.
    const std::string OUTPUT_COLLECTOR_HOST_REFRESH_SECONDS = "output_collector_host_refresh_seconds";
    const size_t OUTPUT_COLLECTOR_HOST_REFRESH_SECONDS_DEFAULT = 60;

    // The TCP port to send to.
    const std::string OUTPUT_COLLECTOR_PORT = "output_collector_port";
    const size_t OUTPUT_COLLECTOR_PORT_DEFAULT = 8124;

    /**
     * StatsD output settings
     */

    // Whether StatsD export is enabled.
    const std::string OUTPUT_STATSD_ENABLED = "output_statsd_enabled";
    const bool OUTPUT_STATSD_ENABLED_DEFAULT = true;

    // The host to send to. Should be the endpoint for tasks in the monitoring framework.
    const std::string OUTPUT_STATSD_HOST = "output_statsd_host";
    const std::string OUTPUT_STATSD_HOST_DEFAULT = "metrics.marathon.mesos";

    // The period in seconds between host resolutions. Automatically detects changes in DNS records,
    // with automatic selection of a random A record if multiple entries are configured.
    const std::string OUTPUT_STATSD_HOST_REFRESH_SECONDS = "output_statsd_host_refresh_seconds";
    const size_t OUTPUT_STATSD_HOST_REFRESH_SECONDS_DEFAULT = 60;

    // The UDP port to send to.
    const std::string OUTPUT_STATSD_PORT = "output_statsd_port";
    const size_t OUTPUT_STATSD_PORT_DEFAULT = 8125;

    // How to annotate output with metadata about the originating containers.
    const std::string OUTPUT_STATSD_ANNOTATION_MODE = "output_statsd_annotation_mode";
    const std::string OUTPUT_STATSD_ANNOTATION_MODE_NONE = "none";
    const std::string OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG = "tag_datadog";
    const std::string OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX = "key_prefix";
    const std::string OUTPUT_STATSD_ANNOTATION_MODE_DEFAULT = OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX;

    // Whether to group output metrics into a smaller number of packets.
    const std::string OUTPUT_STATSD_CHUNKING = "output_statsd_chunking";
    const bool OUTPUT_STATSD_CHUNKING_DEFAULT = true;

    // The MTU to enforce for chunked packets.
    const std::string OUTPUT_STATSD_CHUNK_SIZE_BYTES = "output_statsd_chunk_size_bytes";
    const int OUTPUT_STATSD_CHUNK_SIZE_BYTES_DEFAULT = 512;

    /**
     * Container cache settings
     */

    // Directory to store state data for recovery if the agent process is restarted.
    // This list will be automatically updated as containers are added/removed from the agent.
    // See input_state_cache_impl.cpp for directory structure.
    const std::string STATE_PATH_DIR = "state_path_dir";
    // Seems to be the convention. See eg 'cni/paths.hpp' in stock mesos isolators
    const std::string STATE_PATH_DIR_DEFAULT = "/var/run/mesos/isolators/com_mesosphere_MetricsIsolatorModule/";

    std::string get_str(const mesos::Parameters& parameters, const std::string& key, const std::string& default_value);
    size_t get_uint(const mesos::Parameters& parameters, const std::string& key, size_t default_value);
    bool get_bool(const mesos::Parameters& parameters, const std::string& key, bool default_value);
  }
}
