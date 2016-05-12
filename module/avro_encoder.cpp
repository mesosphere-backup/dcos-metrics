#include "avro_encoder.hpp"

#include <sys/time.h>
#include <glog/logging.h>
#include <unordered_map>

#include <avro/Compiler.hh>
#include <avro/Encoder.hh>
#include "metrics_schema_struct.hpp"
#include "metrics_schema_json.hpp"

namespace {
  /**
   * Tags to use when there's a data issue.
   */
  const std::string UNKNOWN_METRIC_TAG("unknown_metric");
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
  typedef std::map<std::string, std::vector<uint8_t>> MetadataMap;
  typedef boost::array<uint8_t, 16> DataFileSync;
  typedef boost::array<uint8_t, 4> Magic;

  const size_t MALLOC_BLOCK_SIZE = 64 * 1024;
  const Magic magic = { { 'O', 'b', 'j', '\x01' } };

  void set_metadata(MetadataMap& map, const std::string& key, const std::string& value) {
    MetadataVal value_conv(value.size());
    std::copy(value.begin(), value.end(), value_conv.begin());
    map[key] = value_conv;
  }

  void encode_header(avro::Encoder& encoder, avro::OutputStream& out) {
    MetadataMap metadata_map;
    set_metadata(metadata_map, AVRO_CODEC_KEY, AVRO_NULL_CODEC);

    LOG(INFO) << "SCHEMA BEGIN\n\n" << metrics_schema::SCHEMA_JSON << "\n\nSCHEMA END";//TODO TEMP
    avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);
    std::ostringstream oss;
    schema.toJson(oss);
    LOG(INFO) << "PARSED SCHEMA BEGIN\n\n" << oss.str() << "\n\nPARSED SCHEMA END";//TODO TEMP
    set_metadata(metadata_map, AVRO_SCHEMA_KEY, oss.str());

    DataFileSync sync;
    for (size_t i = 0; i < sync.size(); ++i) {
      sync[i] = random();
    }

    encoder.init(out);
    avro::encode(encoder, magic);
    avro::encode(encoder, metadata_map);
    avro::encode(encoder, sync);
    encoder.flush();
  }

  void init_list(
      metrics_schema::MetricList& list,
      const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info) {
    if (container_id != NULL && executor_info != NULL) {
      list.tags.resize(3);

      metrics_schema::Tag& tag = list.tags[0];
      tag.key = CONTAINER_ID_AVRO_KEY;
      tag.value = container_id->value();

      tag = list.tags[1];
      tag.key = EXECUTOR_ID_AVRO_KEY;
      tag.value = executor_info->executor_id().value();

      tag = list.tags[2];
      tag.key = FRAMEWORK_ID_AVRO_KEY;
      tag.value = executor_info->framework_id().value();

      list.topic = tag.value; // use framework_id as topic
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

  void parse_statsd_name_val_tags(const char* data, size_t size, metrics_schema::Datapoint& point) {
    // Expected format:
    // name:val|type[|@0.3][|#tag1:val1,tag2:val2]
    char* name_end = (char*)memchr(data, ':', size);
    if (name_end == NULL) {
      // name ending delim not found. corrupt data? treat as 'name = 0'
      point.name.insert(0, data, size);
      point.value = 0;
      return;
    }
    size_t name_len = name_end - data;
    point.name.insert(0, data, name_len);
    if (name_len == size) {
      // no space for value after name, exit early
      point.value = 0;
      return;
    }

    char* val_end = (char*)memchr(name_end, '|', size - name_len);
    // note: we use std::stod(std::str) instead of strtod(char*) here, to avoid passing the buffer
    //       (why doesn't strntod(char*, size) exist...)
    // in both cases, go forward a character to skip the ':' following the name:
    if (val_end == NULL) {
      // val ending delim not found: just parse everything after name as the value
      std::string val_str(name_end + 1, size - name_len - 1);
      point.value = std::stod(val_str);
    } else {
      std::string val_str(name_end + 1, val_end - name_end - 1);
      point.value = std::stod(val_str);
    }

    //TODO parse_datadog_tags(metric_list.tags, in_data, in_size);
  }

  std::string cached_header;
}

std::string metrics::AvroEncoder::header() {
  std::shared_ptr<avro::OutputStream> outstream(avro::memoryOutputStream());
  avro::EncoderPtr encoder = avro::binaryEncoder();
  encode_header(*encoder, *outstream);
  std::shared_ptr<avro::InputStream> in(avro::memoryInputStream(*outstream));

  std::string header;
  const uint8_t* in_data;
  size_t in_len;
  while (in->next(&in_data, &in_len)) {
    header.append((char*)in_data, in_len);
  }
  return header;
}

size_t metrics::AvroEncoder::encode_metrics(
    const container_id_map<metrics_schema::MetricList>& metric_map,
    const metrics_schema::MetricList& metric_list,
    char** out) {
  std::shared_ptr<avro::OutputStream> outstream(avro::memoryOutputStream());
  avro::EncoderPtr encoder = avro::binaryEncoder();
  encode_header(*encoder, *outstream);
  for (auto entry : metric_map) {
    avro::encode(*encoder, entry.second);
  }
  if (!metric_list.datapoints.empty()) {
    avro::encode(*encoder, metric_list);
  }
  //NOTE: instream implementation doesn't copy the stream, however this means that outstream
  //      MUST STAY IN SCOPE while instream is used
  std::shared_ptr<avro::InputStream> in(avro::memoryInputStream(*outstream));

  //TODO can we instead stream directly into the socket or something?
  size_t out_capacity = MALLOC_BLOCK_SIZE;
  size_t out_used = 0;
  *out = (char*)malloc(out_capacity);

  const uint8_t* in_data;
  size_t in_len;
  while (in->next(&in_data, &in_len)) {
    while (out_used + in_len > out_capacity) {
      // increase buffer size to fit input data
      out_capacity += MALLOC_BLOCK_SIZE;
      char* check = (char*)realloc(*out, out_capacity);
      if (check == NULL) {
        // out of memory, give up
        free(*out);
        return 0;
      }
      *out = check;
    }
    memcpy(*out + out_used, in_data, in_len);
    out_used += in_len;
  }
  return out_used;
}

size_t metrics::AvroEncoder::statsd_to_struct(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* data, size_t size,
    container_id_map<metrics_schema::MetricList>& metric_map) {
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

  size_t old_size = list_out->datapoints.size();
  list_out->datapoints.resize(old_size + 1);
  metrics_schema::Datapoint& point = list_out->datapoints[old_size];
  point.time = now_in_ms();
  parse_statsd_name_val_tags(data, size, point);
  return 1;
}

size_t metrics::AvroEncoder::statsd_to_struct(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* data, size_t size,
    metrics_schema::MetricList& metric_list) {
  init_list(metric_list, container_id, executor_info);
  size_t old_size = metric_list.datapoints.size();
  metric_list.datapoints.resize(old_size + 1);
  metrics_schema::Datapoint& point = metric_list.datapoints[old_size];
  point.time = now_in_ms();
  parse_statsd_name_val_tags(data, size, point);
  return 1;
}

size_t metrics::AvroEncoder::resources_to_struct(
    const mesos::ResourceUsage& usage, metrics_schema::MetricList& metric_list) {
  LOG(INFO) << "Resources:\n" << usage.DebugString();
  if (metric_list.topic.empty()) {
    //TODO init topic + tags
  }
  //TODO populate metric_list
  return 0; // TODO return num datapoints added
}
