#pragma once

#include <stddef.h>

#include <mesos/mesos.pb.h>

namespace stats {
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

    namespace dest_mode {
      enum Value { UNKNOWN, NONE, STATSD_UDP, POLL_TCP };
    }
    dest_mode::Value to_dest_mode(const std::string& param);

    /**
     * Input settings
     */

    // The host to listen on. Should stay with "localhost" except in ip-per-container environments.
    const std::string LISTEN_HOST = "listen_host";
    const std::string LISTEN_HOST_DEFAULT = "127.0.0.1";

    // The mode to use for assigning containers to listen ports.
    const std::string LISTEN_PORT_MODE = "listen_port_mode";

    // Listens to only a single port across the slave. Only advisable in ip-per-container environments.
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
     * Output settings
     */

    // The type of host connection to use for DEST_HOST.
    const std::string DEST_MODE = "dest_mode";
    const std::string DEST_MODE_NONE = "none";
    const std::string DEST_MODE_POLL_TCP = "poll_tcp";
    const std::string DEST_MODE_STATSD_UDP = "statsd_udp";
    const std::string DEST_MODE_DEFAULT = DEST_MODE_STATSD_UDP;

    // The host to send to. Should be the endpoint for tasks in the monitoring framework.
    const std::string DEST_HOST = "dest_host";
    const std::string DEST_HOST_DEFAULT = "statsd.monitoring.mesos";

    // The period in seconds between host resolutions. Automatically detects changes in DNS records,
    // with automatic selection of a random A record if multiple entries are configured.
    const std::string DEST_REFRESH_SECONDS = "dest_refresh_seconds";
    const size_t DEST_REFRESH_SECONDS_DEFAULT = 300; // 5 minutes

    // The port to send to.
    const std::string DEST_PORT = "dest_port";
    const size_t DEST_PORT_DEFAULT = 8125;

    // How to annotate output with metadata about the originating containers.
    const std::string ANNOTATION_MODE = "annotation_mode";
    const std::string ANNOTATION_MODE_NONE = "none";
    const std::string ANNOTATION_MODE_TAG_DATADOG = "tag_datadog";
    const std::string ANNOTATION_MODE_KEY_PREFIX = "key_prefix";
    const std::string ANNOTATION_MODE_DEFAULT = ANNOTATION_MODE_TAG_DATADOG;

    // Whether to group output stats into a smaller number of packets.
    const std::string CHUNKING = "chunking";
    const bool CHUNKING_DEFAULT = true;

    // The MTU to enforce for chunked packets.
    const std::string CHUNK_SIZE_BYTES = "chunk_size_bytes";
    const int CHUNK_SIZE_BYTES_DEFAULT = 512;

    /**
     * On-system settings
     */

    // Directory to store state data for recovery if the agent process is restarted
    // Structure inside will be:
    // <state_path_dir>/
    //  |-- container-<container id 1>.json (contains the assigned host/port for this container)
    //  |-- container-<container id 2>.json (...)
    //  |-- ...
    // This list will be automatically updated as containers are added/removed from the agent.
    const std::string STATE_PATH_DIR = "state_path_dir";
    // Seems to be the convention. See eg 'cni/paths.hpp' in stock mesos isolators
    const std::string STATE_PATH_DIR_DEFAULT = "/var/run/mesos/isolators/network/metrics";

    std::string get_str(const mesos::Parameters& parameters, const std::string& key, const std::string& default_value);
    size_t get_uint(const mesos::Parameters& parameters, const std::string& key, size_t default_value);
    bool get_bool(const mesos::Parameters& parameters, const std::string& key, bool default_value);
  }
}
