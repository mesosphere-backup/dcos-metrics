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
  const DataFileSync sync_bytes_ = { {//TODO TEMP useful for debugging
      'F', 'E', 'F', 'E',
      'F', 'E', 'F', 'E',
      'F', 'E', 'F', 'E',
      'F', 'E', 'F', 'E' } };
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
    const metrics_schema::MetricList& metric_list,
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
    if (!empty(metric_list)) {
      ++obj_count;
      avro::encode(*encoder, metric_list);
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
      init_list(*list_out, NULL, NULL);
    } else {
      list_out = &iter->second;
    }
  } else {
    auto iter = metric_map.find(*container_id);
    if (iter == metric_map.end()) {
      list_out = &metric_map[*container_id];
      init_list(*list_out, container_id, executor_info);
    } else {
      list_out = &iter->second;
    }
  }

  // Spawn/update a Datapoint directly within the list
  size_t old_size = list_out->datapoints.size();
  list_out->datapoints.resize(old_size + 1);
  metrics_schema::Datapoint& point = list_out->datapoints[old_size];
  point.time_ms = now_in_ms();
  parse_statsd_name_val_tags(data, size, point, list_out->tags);

  return 1;
}

size_t metrics::AvroEncoder::statsd_to_struct(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* data, size_t size,
    metrics_schema::MetricList& metric_list) {
  init_list(metric_list, container_id, executor_info);

  // Spawn/update a Datapoint directly within the list
  size_t old_size = metric_list.datapoints.size();
  metric_list.datapoints.resize(old_size + 1);
  metrics_schema::Datapoint& point = metric_list.datapoints[old_size];
  point.time_ms = now_in_ms();
  parse_statsd_name_val_tags(data, size, point, metric_list.tags);

  return 1;
}

size_t metrics::AvroEncoder::resources_to_struct(
    const mesos::ResourceUsage& usage, metrics_schema::MetricList& metric_list) {
  LOG(INFO) << "Resources:\n" << usage.DebugString();
  //TODO implement ResourceUsage -> avro
  return 0;
}

bool metrics::AvroEncoder::empty(const metrics_schema::MetricList& metric_list) {
  return metric_list.datapoints.empty() && metric_list.tags.empty() && metric_list.topic.empty();
}
