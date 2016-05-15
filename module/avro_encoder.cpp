#include "avro_encoder.hpp"

#include <boost/asio/streambuf.hpp>
#include <glog/logging.h>
#include <sys/time.h>
#include <unordered_map>

#include <avro/Compiler.hh>
#include <avro/Encoder.hh>
#include "metrics_schema_struct.hpp"
#include "metrics_schema_json.hpp"

namespace {
  /**
   * Tags to use when there's a data issue.
   */
  const std::string UNKNOWN_CONTAINER_TAG("unknown_container");

  /**
   * Tag names to use for avro tags
   */
  const std::string CONTAINER_ID_AVRO_KEY("container_id");
  const std::string EXECUTOR_ID_AVRO_KEY("executor_id");
  const std::string FRAMEWORK_ID_AVRO_KEY("framework_id");

  /**
   * Avro C++'s DataFile.cc requires writing to a filename, so let's DIY the file header.
   */
  const std::string AVRO_SCHEMA_KEY("avro.schema");
  const std::string AVRO_CODEC_KEY("avro.codec");
  const std::string AVRO_NULL_CODEC("null");
  const std::string AVRO_DEFLATE_CODEC("deflate");//FIXME support deflate

  typedef std::vector<uint8_t> MetadataVal;
  typedef std::map<std::string, MetadataVal> MetadataMap;

  const size_t MALLOC_BLOCK_SIZE = 64 * 1024;
  const boost::array<uint8_t, 4> magic = { { 'O', 'b', 'j', '\x01' } };

  void set_metadata(MetadataMap& map, const std::string& key, const std::string& value) {
    MetadataVal value_conv(value.size());
    std::copy(value.begin(), value.end(), value_conv.begin());
    map[key] = value_conv;
  }

  void add_tag(std::vector<metrics_schema::Tag>& tags,
      const std::string& key, const std::string& value) {
    tags.resize(tags.size() + 1);
    metrics_schema::Tag& tag = tags[tags.size() - 1];
    tag.key = key;
    tag.value = value;
  }

  void init_list(
      metrics_schema::MetricList& list,
      const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info) {
    if (container_id != NULL && executor_info != NULL) {
      if (list.topic.empty()) {
        list.topic = executor_info->framework_id().value();
      }

      bool found_framework_id = false,
        found_executor_id = false,
        found_container_id = false;
      for (const metrics_schema::Tag& tag : list.tags) {
        if (tag.key == FRAMEWORK_ID_AVRO_KEY) {
          found_framework_id = true;
        } else if (tag.key == EXECUTOR_ID_AVRO_KEY) {
          found_executor_id = true;
        } else if (tag.key == CONTAINER_ID_AVRO_KEY) {
          found_container_id = true;
        }
      }
      if (!found_framework_id) {
        add_tag(list.tags, FRAMEWORK_ID_AVRO_KEY, executor_info->framework_id().value());
      }
      if (!found_executor_id) {
        add_tag(list.tags, EXECUTOR_ID_AVRO_KEY, executor_info->executor_id().value());
      }
      if (!found_container_id) {
        add_tag(list.tags, CONTAINER_ID_AVRO_KEY, container_id->value());
      }
    } else {
      list.topic = UNKNOWN_CONTAINER_TAG;
    }
  }

  int64_t now_in_ms() {
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
      return 0;
    }
    // convert result to ms
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
  }

  void parse_datadog_tags(const char* data, size_t size, std::vector<metrics_schema::Tag>& tags) {
    // Expected format: |#tag:val,tag2:val2,(...)
    //TODO parse, while avoiding conflicting tags across datapoints...
  }

  void parse_statsd_name_val_tags(const char* data, size_t size,
      metrics_schema::Datapoint& point, std::vector<metrics_schema::Tag>& tags) {
    // Expected format:
    // name[:val][|section][|@0.3][|#tag1:val1,tag2:val2][|...]
    // first, find the start of any extra sections: we want to avoid going too far when searching for ':'s.
    char* section_start = (char*)memchr(data, '|', size);
    size_t nameval_size = size;
    if (section_start != NULL) {
      nameval_size = section_start - data;
    }

    char* name_end = (char*)memchr(data, ':', nameval_size);
    if (name_end == NULL) {
      // value delim not found in nameval region. missing value? treat as 'name = 0'
      point.name.insert(0, data, nameval_size);
      point.value = 0;
    } else {
      size_t name_len = name_end - data;
      point.name.insert(0, data, name_len);

      size_t value_len;
      if (section_start == NULL) {
        // section delim not found: parse until end of buffer
        value_len = size - name_len;
      } else {
        // parse until section delim
        value_len = section_start - name_end;
      }

      // note: we use std::stod(std::str) instead of strtod(char*) here, to avoid escaping buffer
      //       (wish strntod(char*, size) was available...)
      std::string val_str(name_end + 1, value_len - 1); // exclude ':' delim
      try {
        point.value = std::stod(val_str);
      } catch (...) {
        LOG(WARNING) << "Corrupt statsd value: '" << val_str << "' "
                     << "(from data '" << std::string(data, size) << "')";
        point.value = 0;
      }
    }

    while (section_start != NULL) {
      // parse any following sections (eg |@0.1 sampling or |#tag1:val1,tag2:val2)
      size_t sections_size = (data + size) - section_start;
      if (sections_size <= 2) {
        break;
      }
      // find start of next section, if any
      char* next_section_start = (char*)memchr(section_start + 1, '|', sections_size - 1);
      char* section_end =
        (next_section_start == NULL) ? (char*)(data + size) : next_section_start;
      switch (section_start[1]) {
        case '@': {
          // sampling: multiply value to correct it
          std::string factor_str(section_start + 2, section_end - section_start - 2);
          try {
            double sample_factor = std::stod(factor_str);
            if (sample_factor != 0) {
              point.value /= sample_factor;
            } else {
              throw std::invalid_argument("Zero sampling is invalid");
            }
          } catch (...) {
            LOG(WARNING) << "Corrupt sampling value: '" << factor_str << "' "
                         << "(from data '" << std::string(data, size) << "')";
          }
          break;
        }
        case '#':
          // datadog tags: include in our tags
          parse_datadog_tags(section_start, section_end - section_start, tags);
          break;
      }
      // seek to next section, if any
      if (next_section_start == NULL) {
        break;
      }
      section_start = next_section_start;
    }
  }

  typedef boost::array<uint8_t, 16> DataFileSync;
  const DataFileSync sync_bytes_ = { {//TODO TEMP useful for debugging/logging
      '\n', 'E', 'F', 'E',
      'F', 'E', 'F', 'E',
      'F', 'E', 'F', 'E',
      'F', 'E', 'F', '\n' } };
  std::shared_ptr<DataFileSync> sync_bytes;
  std::string header_data, footer_data;

  std::shared_ptr<DataFileSync> get_sync_bytes() {
    if (!sync_bytes) {
      /*
      sync_bytes.reset(new DataFileSync);
      for (size_t i = 0; i < sync_bytes->size(); ++i) {
        (*sync_bytes)[i] = random();
      }
      */
      sync_bytes.reset(new DataFileSync(sync_bytes_));
    }
    return sync_bytes;
  }
}

const std::string& metrics::AvroEncoder::header() {
  if (header_data.empty()) {
    std::ostringstream oss;
    {
      std::shared_ptr<avro::OutputStream> avro_outstream(avro::ostreamOutputStream(oss));
      avro::EncoderPtr encoder = avro::binaryEncoder();
      encoder->init(*avro_outstream);

      MetadataMap metadata_map;
      set_metadata(metadata_map, AVRO_CODEC_KEY, AVRO_NULL_CODEC);

      // Pass minimized schema directly. Avro C++'s compileJsonSchemaFromString just de-minimizes it.
      LOG(INFO) << "Using schema: " << metrics_schema::SCHEMA_JSON;
      set_metadata(metadata_map, AVRO_SCHEMA_KEY, metrics_schema::SCHEMA_JSON);

      avro::encode(*encoder, magic);
      avro::encode(*encoder, metadata_map);
      avro::encode(*encoder, *get_sync_bytes());
      encoder->flush(); // required
    }
    header_data = oss.str();
  }

  return header_data;
}

void metrics::AvroEncoder::encode_metrics_block(
    const container_id_ord_map<metrics_schema::MetricList>& metric_map,
    std::ostream& ostream) {
  // in the first pass, encode the data so that we can get the byte count
  int64_t obj_count = 0;
  std::ostringstream oss;
  {
    std::shared_ptr<avro::OutputStream> avro_ostream(avro::ostreamOutputStream(oss));
    avro::EncoderPtr encoder = avro::binaryEncoder();
    encoder->init(*avro_ostream);

    for (auto entry : metric_map) {
      if (!empty(entry.second)) {
        ++obj_count;
        avro::encode(*encoder, entry.second);
      }
    }
    if (obj_count == 0) {
      // Nothing to encode, produce 0 bytes
      return;
    }

    encoder->flush();
  }

  // in the second pass, write the block:
  // - block header (obj count + byte count)
  // - the encoded data (from first pass)
  // - block footer (sync bytes)
  std::shared_ptr<avro::OutputStream> avro_ostream(avro::ostreamOutputStream(ostream));
  avro::EncoderPtr encoder = avro::binaryEncoder();
  encoder->init(*avro_ostream);
  avro::encode(*encoder, obj_count);
  avro::encode(*encoder, (int64_t)oss.str().size());

  encoder->flush(); // ensure header is written before we write data
  ostream << oss.str();

  avro::encode(*encoder, *get_sync_bytes());
  encoder->flush(); // required
}

size_t metrics::AvroEncoder::statsd_to_struct(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* data, size_t size,
    container_id_ord_map<metrics_schema::MetricList>& metric_map) {
  metrics_schema::MetricList* list_out;
  if (container_id == NULL) {
    mesos::ContainerID missing_id;
    missing_id.set_value(UNKNOWN_CONTAINER_TAG);
    auto iter = metric_map.find(missing_id);
    if (iter == metric_map.end()) {
      list_out = &metric_map[missing_id];
    } else {
      list_out = &iter->second;
    }
    init_list(*list_out, NULL, NULL);
  } else {
    auto iter = metric_map.find(*container_id);
    if (iter == metric_map.end()) {
      list_out = &metric_map[*container_id];
    } else {
      list_out = &iter->second;
    }
    init_list(*list_out, container_id, executor_info);
  }

  // Spawn/update a Datapoint directly within the list
  size_t old_size = list_out->datapoints.size();
  list_out->datapoints.resize(old_size + 1);
  metrics_schema::Datapoint& point = list_out->datapoints[old_size];
  point.time_ms = now_in_ms();
  parse_statsd_name_val_tags(data, size, point, list_out->tags);

  return 1;
}

#define ADD_STAT(COUNTER, OBJ, FIELDPREFIX, FIELDNAME)                  \
  if (OBJ.has_##FIELDNAME()) {                                          \
    ++COUNTER;                                                          \
    d.name = FIELDPREFIX "." #FIELDNAME ;                               \
    d.value = OBJ.FIELDNAME();                                          \
    list.datapoints.push_back(d);                                       \
  }
#define ADD_NAMED_STAT(COUNTER, OBJ, FIELDPREFIX, NAMESTR, FIELDNAME)   \
  if (OBJ.has_##FIELDNAME()) {                                          \
    ++COUNTER;                                                          \
    std::ostringstream oss;                                             \
    oss << FIELDPREFIX "." << NAMESTR << "." #FIELDNAME ;               \
    d.name = oss.str();                                                 \
    d.value = OBJ.FIELDNAME();                                          \
    list.datapoints.push_back(d);                                       \
  }

namespace {
  size_t add_perf(
      const mesos::PerfStatistics& perf, metrics_schema::Datapoint& d, metrics_schema::MetricList& list) {
    size_t vals = 0;

    ADD_STAT(vals, perf, "perf", cycles);
    ADD_STAT(vals, perf, "perf", stalled_cycles_frontend);
    ADD_STAT(vals, perf, "perf", stalled_cycles_backend);
    ADD_STAT(vals, perf, "perf", instructions);
    ADD_STAT(vals, perf, "perf", cache_references);
    ADD_STAT(vals, perf, "perf", cache_misses);
    ADD_STAT(vals, perf, "perf", branches);
    ADD_STAT(vals, perf, "perf", branch_misses);
    ADD_STAT(vals, perf, "perf", bus_cycles);
    ADD_STAT(vals, perf, "perf", ref_cycles);

    ADD_STAT(vals, perf, "perf", cpu_clock);
    ADD_STAT(vals, perf, "perf", task_clock);
    ADD_STAT(vals, perf, "perf", page_faults);
    ADD_STAT(vals, perf, "perf", minor_faults);
    ADD_STAT(vals, perf, "perf", major_faults);
    ADD_STAT(vals, perf, "perf", context_switches);
    ADD_STAT(vals, perf, "perf", cpu_migrations);
    ADD_STAT(vals, perf, "perf", alignment_faults);
    ADD_STAT(vals, perf, "perf", emulation_faults);

    ADD_STAT(vals, perf, "perf", l1_dcache_loads);
    ADD_STAT(vals, perf, "perf", l1_dcache_load_misses);
    ADD_STAT(vals, perf, "perf", l1_dcache_stores);
    ADD_STAT(vals, perf, "perf", l1_dcache_store_misses);
    ADD_STAT(vals, perf, "perf", l1_dcache_prefetches);
    ADD_STAT(vals, perf, "perf", l1_dcache_prefetch_misses);
    ADD_STAT(vals, perf, "perf", l1_icache_loads);
    ADD_STAT(vals, perf, "perf", l1_icache_load_misses);
    ADD_STAT(vals, perf, "perf", l1_icache_prefetches);
    ADD_STAT(vals, perf, "perf", l1_icache_prefetch_misses);
    ADD_STAT(vals, perf, "perf", llc_loads);
    ADD_STAT(vals, perf, "perf", llc_load_misses);
    ADD_STAT(vals, perf, "perf", llc_stores);
    ADD_STAT(vals, perf, "perf", llc_store_misses);
    ADD_STAT(vals, perf, "perf", llc_prefetches);
    ADD_STAT(vals, perf, "perf", llc_prefetch_misses);
    ADD_STAT(vals, perf, "perf", dtlb_loads);
    ADD_STAT(vals, perf, "perf", dtlb_load_misses);
    ADD_STAT(vals, perf, "perf", dtlb_stores);
    ADD_STAT(vals, perf, "perf", dtlb_store_misses);
    ADD_STAT(vals, perf, "perf", dtlb_prefetches);
    ADD_STAT(vals, perf, "perf", dtlb_prefetch_misses);
    ADD_STAT(vals, perf, "perf", itlb_loads);
    ADD_STAT(vals, perf, "perf", itlb_load_misses);
    ADD_STAT(vals, perf, "perf", branch_loads);
    ADD_STAT(vals, perf, "perf", branch_load_misses);
    ADD_STAT(vals, perf, "perf", node_loads);
    ADD_STAT(vals, perf, "perf", node_load_misses);
    ADD_STAT(vals, perf, "perf", node_stores);
    ADD_STAT(vals, perf, "perf", node_store_misses);
    ADD_STAT(vals, perf, "perf", node_prefetches);
    ADD_STAT(vals, perf, "perf", node_prefetch_misses);

    return vals;
  }

  size_t add_traf(const mesos::TrafficControlStatistics& traf,
      metrics_schema::Datapoint& d, metrics_schema::MetricList& list) {
    size_t vals = 0;

    const std::string& id = traf.id();
    ADD_NAMED_STAT(vals, traf, "traf", id, backlog);
    ADD_NAMED_STAT(vals, traf, "traf", id, bytes);
    ADD_NAMED_STAT(vals, traf, "traf", id, drops);
    ADD_NAMED_STAT(vals, traf, "traf", id, overlimits);
    ADD_NAMED_STAT(vals, traf, "traf", id, packets);
    ADD_NAMED_STAT(vals, traf, "traf", id, qlen);
    ADD_NAMED_STAT(vals, traf, "traf", id, ratebps);
    ADD_NAMED_STAT(vals, traf, "traf", id, ratepps);
    ADD_NAMED_STAT(vals, traf, "traf", id, requeues);

    return vals;
  }

  size_t add_snmp_ip(
      const mesos::IpStatistics& ip, metrics_schema::Datapoint& d, metrics_schema::MetricList& list) {
    size_t vals = 0;

    ADD_STAT(vals, ip, "snmp.ip", forwarding);
    ADD_STAT(vals, ip, "snmp.ip", defaultttl);
    ADD_STAT(vals, ip, "snmp.ip", inreceives);
    ADD_STAT(vals, ip, "snmp.ip", inhdrerrors);
    ADD_STAT(vals, ip, "snmp.ip", inaddrerrors);
    ADD_STAT(vals, ip, "snmp.ip", forwdatagrams);
    ADD_STAT(vals, ip, "snmp.ip", inunknownprotos);
    ADD_STAT(vals, ip, "snmp.ip", indiscards);
    ADD_STAT(vals, ip, "snmp.ip", indelivers);
    ADD_STAT(vals, ip, "snmp.ip", outrequests);
    ADD_STAT(vals, ip, "snmp.ip", outdiscards);
    ADD_STAT(vals, ip, "snmp.ip", outnoroutes);
    ADD_STAT(vals, ip, "snmp.ip", reasmtimeout);
    ADD_STAT(vals, ip, "snmp.ip", reasmreqds);
    ADD_STAT(vals, ip, "snmp.ip", reasmoks);
    ADD_STAT(vals, ip, "snmp.ip", reasmfails);
    ADD_STAT(vals, ip, "snmp.ip", fragoks);
    ADD_STAT(vals, ip, "snmp.ip", fragfails);
    ADD_STAT(vals, ip, "snmp.ip", fragcreates);

    return vals;
  }

  size_t add_snmp_icmp(
      const mesos::IcmpStatistics& icmp, metrics_schema::Datapoint& d, metrics_schema::MetricList& list) {
    size_t vals = 0;

    ADD_STAT(vals, icmp, "snmp.icmp", inmsgs);
    ADD_STAT(vals, icmp, "snmp.icmp", inerrors);
    ADD_STAT(vals, icmp, "snmp.icmp", incsumerrors);
    ADD_STAT(vals, icmp, "snmp.icmp", indestunreachs);
    ADD_STAT(vals, icmp, "snmp.icmp", intimeexcds);
    ADD_STAT(vals, icmp, "snmp.icmp", inparmprobs);
    ADD_STAT(vals, icmp, "snmp.icmp", insrcquenchs);
    ADD_STAT(vals, icmp, "snmp.icmp", inredirects);
    ADD_STAT(vals, icmp, "snmp.icmp", inechos);
    ADD_STAT(vals, icmp, "snmp.icmp", inechoreps);
    ADD_STAT(vals, icmp, "snmp.icmp", intimestamps);
    ADD_STAT(vals, icmp, "snmp.icmp", intimestampreps);
    ADD_STAT(vals, icmp, "snmp.icmp", inaddrmasks);
    ADD_STAT(vals, icmp, "snmp.icmp", inaddrmaskreps);
    ADD_STAT(vals, icmp, "snmp.icmp", outmsgs);
    ADD_STAT(vals, icmp, "snmp.icmp", outerrors);
    ADD_STAT(vals, icmp, "snmp.icmp", outdestunreachs);
    ADD_STAT(vals, icmp, "snmp.icmp", outtimeexcds);
    ADD_STAT(vals, icmp, "snmp.icmp", outparmprobs);
    ADD_STAT(vals, icmp, "snmp.icmp", outsrcquenchs);
    ADD_STAT(vals, icmp, "snmp.icmp", outredirects);
    ADD_STAT(vals, icmp, "snmp.icmp", outechos);
    ADD_STAT(vals, icmp, "snmp.icmp", outechoreps);
    ADD_STAT(vals, icmp, "snmp.icmp", outtimestamps);
    ADD_STAT(vals, icmp, "snmp.icmp", outtimestampreps);
    ADD_STAT(vals, icmp, "snmp.icmp", outaddrmasks);
    ADD_STAT(vals, icmp, "snmp.icmp", outaddrmaskreps);

    return vals;
  }

  size_t add_snmp_tcp(
      const mesos::TcpStatistics& tcp, metrics_schema::Datapoint& d, metrics_schema::MetricList& list) {
    size_t vals = 0;

    ADD_STAT(vals, tcp, "snmp.tcp", rtoalgorithm);
    ADD_STAT(vals, tcp, "snmp.tcp", rtomin);
    ADD_STAT(vals, tcp, "snmp.tcp", rtomax);
    ADD_STAT(vals, tcp, "snmp.tcp", maxconn);
    ADD_STAT(vals, tcp, "snmp.tcp", activeopens);
    ADD_STAT(vals, tcp, "snmp.tcp", passiveopens);
    ADD_STAT(vals, tcp, "snmp.tcp", attemptfails);
    ADD_STAT(vals, tcp, "snmp.tcp", estabresets);
    ADD_STAT(vals, tcp, "snmp.tcp", currestab);
    ADD_STAT(vals, tcp, "snmp.tcp", insegs);
    ADD_STAT(vals, tcp, "snmp.tcp", outsegs);
    ADD_STAT(vals, tcp, "snmp.tcp", retranssegs);
    ADD_STAT(vals, tcp, "snmp.tcp", inerrs);
    ADD_STAT(vals, tcp, "snmp.tcp", outrsts);
    ADD_STAT(vals, tcp, "snmp.tcp", incsumerrors);

    return vals;
  }

  size_t add_snmp_udp(
      const mesos::UdpStatistics& udp, metrics_schema::Datapoint& d, metrics_schema::MetricList& list) {
    size_t vals = 0;

    ADD_STAT(vals, udp, "snmp.udp", indatagrams);
    ADD_STAT(vals, udp, "snmp.udp", noports);
    ADD_STAT(vals, udp, "snmp.udp", inerrors);
    ADD_STAT(vals, udp, "snmp.udp", outdatagrams);
    ADD_STAT(vals, udp, "snmp.udp", rcvbuferrors);
    ADD_STAT(vals, udp, "snmp.udp", sndbuferrors);
    ADD_STAT(vals, udp, "snmp.udp", incsumerrors);
    ADD_STAT(vals, udp, "snmp.udp", ignoredmulti);

    return vals;
  }
}

/** Sample:
I0514 17:24:36.037711  1948 avro_encoder.cpp:603] Resources:
executors {
  executor_info {
    executor_id {
      value: "metrics-avro.487687aa-19f8-11e6-9695-d6e905db151a"
    }
    resources {
      name: "cpus"
      type: SCALAR
      scalar {
        value: 0.1
      }
      role: "*"
    }
    resources {
      name: "mem"
      type: SCALAR
      scalar {
        value: 32
      }
      role: "*"
    }
    command {
      uris {
        value: "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-receiver.tgz"
        executable: false
        extract: true
        cache: false
      }
      environment {
        variables {
          name: "MARATHON_APP_VERSION"
          value: "2016-05-14T17:20:13.013Z"
        }
        variables {
          name: "HOST"
          value: "10.0.0.31"
        }
        variables {
          name: "MARATHON_APP_RESOURCE_CPUS"
          value: "1.0"
        }
        variables {
          name: "PORT_10000"
          value: "27562"
        }
        variables {
          name: "MESOS_TASK_ID"
          value: "metrics-avro.487687aa-19f8-11e6-9695-d6e905db151a"
        }
        variables {
          name: "PORT"
          value: "27562"
        }
        variables {
          name: "MARATHON_APP_RESOURCE_MEM"
          value: "128.0"
        }
        variables {
          name: "PORTS"
          value: "27562"
        }
        variables {
          name: "MARATHON_APP_RESOURCE_DISK"
          value: "0.0"
        }
        variables {
          name: "MARATHON_APP_LABELS"
          value: ""
        }
        variables {
          name: "MARATHON_APP_ID"
          value: "/metrics-avro"
        }
        variables {
          name: "PORT0"
          value: "27562"
        }
      }
      value: "/opt/mesosphere/packages/mesos--347b03376cc23e6e376cc38887bd6b644cc8a4b6/libexec/mesos/mesos-executor"
      shell: true
    }
    framework_id {
      value: "f4f6878c-783c-463f-85b9-3407e68cc69b-0000"
    }
    name: "Command Executor (Task: metrics-avro.487687aa-19f8-11e6-9695-d6e905db151a) (Command: sh -c \'LD_LIBRARY_P...\')"
    source: "metrics-avro.487687aa-19f8-11e6-9695-d6e905db151a"
  }
  allocated {
    name: "cpus"
    type: SCALAR
    scalar {
      value: 1.1
    }
    role: "*"
  }
  allocated {
    name: "mem"
    type: SCALAR
    scalar {
      value: 160
    }
    role: "*"
  }
  allocated {
    name: "ports"
    type: RANGES
    ranges {
      range {
        begin: 27562
        end: 27562
      }
    }
    role: "*"
  }
  statistics {
    timestamp: 1463246676.03686
    cpus_user_time_secs: 0.03
    cpus_system_time_secs: 0.01
    cpus_limit: 1.1
    mem_rss_bytes: 2584576
    mem_limit_bytes: 167772160
    cpus_nr_periods: 916
    cpus_nr_throttled: 0
    cpus_throttled_time_secs: 0.009895515
    mem_file_bytes: 0
    mem_anon_bytes: 2584576
    mem_mapped_file_bytes: 0
    mem_low_pressure_counter: 0
    mem_medium_pressure_counter: 0
    mem_critical_pressure_counter: 0
    mem_total_bytes: 2584576
    mem_cache_bytes: 0
    mem_swap_bytes: 0
    mem_unevictable_bytes: 0
  }
  container_id {
    value: "2a6199f4-f0e6-4749-be24-f731d1ae1d61"
  }
}
total {
  name: "ports"
  type: RANGES
  ranges {
    range {
      begin: 1025
      end: 2180
    }
    range {
      begin: 2182
      end: 3887
    }
    range {
      begin: 3889
      end: 5049
    }
    range {
      begin: 5052
      end: 8079
    }
    range {
      begin: 8082
      end: 8180
    }
    range {
      begin: 8182
      end: 32000
    }
  }
  role: "*"
}
total {
  name: "disk"
  type: SCALAR
  scalar {
    value: 35572
  }
  role: "*"
}
total {
  name: "cpus"
  type: SCALAR
  scalar {
    value: 4
  }
  role: "*"
}
total {
  name: "mem"
  type: SCALAR
  scalar {
    value: 14019
  }
  role: "*"
}

*/

size_t metrics::AvroEncoder::resources_to_struct(
    const mesos::ResourceUsage& usage,
    container_id_ord_map<metrics_schema::MetricList>& metric_map) {
  size_t vals = 0;
  for (int64_t execi = 0; execi < usage.executors_size(); ++execi) {
    const mesos::ResourceUsage_Executor& executor = usage.executors(execi);

    const mesos::ContainerID& cid = executor.container_id();
    const mesos::ExecutorInfo& einfo = executor.executor_info();
    std::ostringstream oss;
    oss << cid.value() << "-usage";

    mesos::ContainerID custom_id;
    custom_id.set_value(oss.str());//FIXME key against agent rather than against frameworkid?
    metrics_schema::MetricList& list = metric_map[custom_id];
    list.topic = oss.str();// set custom topic before init..:
    init_list(list, &cid, &einfo);

    if (!executor.has_statistics()) {
      continue;
    }

    const mesos::ResourceStatistics& stats = executor.statistics();
    metrics_schema::Datapoint d;
    d.time_ms = (int64_t)(1000 * stats.timestamp());

    ADD_STAT(vals, stats, "usage", processes);
    ADD_STAT(vals, stats, "usage", threads);

    ADD_STAT(vals, stats, "usage", cpus_user_time_secs);
    ADD_STAT(vals, stats, "usage", cpus_system_time_secs);
    ADD_STAT(vals, stats, "usage", cpus_limit);
    ADD_STAT(vals, stats, "usage", cpus_nr_periods);
    ADD_STAT(vals, stats, "usage", cpus_nr_throttled);
    ADD_STAT(vals, stats, "usage", cpus_throttled_time_secs);

    ADD_STAT(vals, stats, "usage", mem_total_bytes);
    ADD_STAT(vals, stats, "usage", mem_total_memsw_bytes);
    ADD_STAT(vals, stats, "usage", mem_limit_bytes);
    ADD_STAT(vals, stats, "usage", mem_soft_limit_bytes);
    ADD_STAT(vals, stats, "usage", mem_file_bytes);
    ADD_STAT(vals, stats, "usage", mem_anon_bytes);
    ADD_STAT(vals, stats, "usage", mem_cache_bytes);
    ADD_STAT(vals, stats, "usage", mem_rss_bytes);
    ADD_STAT(vals, stats, "usage", mem_mapped_file_bytes);
    ADD_STAT(vals, stats, "usage", mem_swap_bytes);
    ADD_STAT(vals, stats, "usage", mem_unevictable_bytes);
    ADD_STAT(vals, stats, "usage", mem_low_pressure_counter);
    ADD_STAT(vals, stats, "usage", mem_medium_pressure_counter);
    ADD_STAT(vals, stats, "usage", mem_critical_pressure_counter);

    ADD_STAT(vals, stats, "usage", disk_limit_bytes);
    ADD_STAT(vals, stats, "usage", disk_used_bytes);

    if (stats.has_perf()) {
      vals += add_perf(stats.perf(), d, list);
    }

    ADD_STAT(vals, stats, "usage", net_rx_packets);
    ADD_STAT(vals, stats, "usage", net_rx_bytes);
    ADD_STAT(vals, stats, "usage", net_rx_errors);
    ADD_STAT(vals, stats, "usage", net_rx_dropped);
    ADD_STAT(vals, stats, "usage", net_tx_packets);
    ADD_STAT(vals, stats, "usage", net_tx_bytes);
    ADD_STAT(vals, stats, "usage", net_tx_errors);
    ADD_STAT(vals, stats, "usage", net_tx_dropped);

    ADD_STAT(vals, stats, "usage", net_tcp_rtt_microsecs_p50);
    ADD_STAT(vals, stats, "usage", net_tcp_rtt_microsecs_p90);
    ADD_STAT(vals, stats, "usage", net_tcp_rtt_microsecs_p95);
    ADD_STAT(vals, stats, "usage", net_tcp_rtt_microsecs_p99);

    ADD_STAT(vals, stats, "usage", net_tcp_active_connections);
    ADD_STAT(vals, stats, "usage", net_tcp_time_wait_connections);

    for (int64_t trafi = 0; trafi < stats.net_traffic_control_statistics_size(); ++trafi) {
      vals += add_traf(stats.net_traffic_control_statistics(trafi), d, list);
    }

    if (stats.has_net_snmp_statistics()) {
      const mesos::SNMPStatistics& snmp = stats.net_snmp_statistics();
      if (snmp.has_ip_stats()) {
        vals += add_snmp_ip(snmp.ip_stats(), d, list);
      }
      if (snmp.has_icmp_stats()) {
        vals += add_snmp_icmp(snmp.icmp_stats(), d, list);
      }
      if (snmp.has_tcp_stats()) {
        vals += add_snmp_tcp(snmp.tcp_stats(), d, list);
      }
      if (snmp.has_udp_stats()) {
        vals += add_snmp_udp(snmp.udp_stats(), d, list);
      }
    }
  }
  //DLOG(INFO) << "Resources:\n" << usage.ShortDebugString();
  return vals;
}

bool metrics::AvroEncoder::empty(const metrics_schema::MetricList& metric_list) {
  return metric_list.datapoints.empty() && metric_list.tags.empty() && metric_list.topic.empty();
}
