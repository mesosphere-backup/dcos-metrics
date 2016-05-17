#include <fstream>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <avro/Compiler.hh>
#include <avro/DataFile.hh>

#include "avro_encoder.hpp"
#include "metrics_schema_json.hpp"

namespace {
  const std::string UNKNOWN("unknown_container");

  inline mesos::ContainerID container_id(const std::string& id) {
    mesos::ContainerID cid;
    cid.set_value(id);
    return cid;
  }
  inline mesos::ExecutorInfo exec_info(const std::string& fid, const std::string& eid) {
    mesos::ExecutorInfo ei;
    ei.mutable_framework_id()->set_value(fid);
    ei.mutable_executor_id()->set_value(eid);
    return ei;
  }
  inline metrics_schema::MetricList metric_list(const std::string& topic) {
    metrics_schema::MetricList ret;
    ret.topic = topic;
    return ret;
  }
  inline metrics_schema::Datapoint datapoint(const std::string& name, int64_t time_ms, double val) {
    metrics_schema::Datapoint ret;
    ret.name = name;
    ret.time_ms = time_ms;
    ret.value = val;
    return ret;
  }
  inline metrics_schema::Tag tag(const std::string& key, const std::string& val) {
    metrics_schema::Tag ret;
    ret.key = key;
    ret.value = val;
    return ret;
  }

  inline metrics::avro_metrics_map_t to_map(
      const metrics_schema::MetricList& list) {
    metrics::avro_metrics_map_t map;
    map[container_id(list.topic)].without_custom_tags = list;
    return map;
  }
  inline metrics::avro_metrics_map_t merge(
      const metrics::avro_metrics_map_t& a,
      const metrics::avro_metrics_map_t& b) {
    metrics::avro_metrics_map_t ret;
    ret.insert(a.begin(), a.end());
    ret.insert(b.begin(), b.end());
    return ret;
  }

  bool check_datapoint(const metrics_schema::MetricList& list,
      double val, const std::string& name) {
    if (list.topic != UNKNOWN) {
      LOG(INFO) << "expected topic " << UNKNOWN << ", got " << list.topic;
      return false;
    }
    if (list.datapoints.size() != 1) {
      LOG(INFO) << "expected datapoint size 1, got " << list.datapoints.size();
      return false;
    }
    if (list.datapoints[0].name != name) {
      LOG(INFO) << "expected name " << name << ", got " << list.datapoints[0].name;
      return false;
    }
    if (list.datapoints[0].time_ms == 0) {
      LOG(INFO) << "expected time_ms NOT zero, got " << list.datapoints[0].time_ms;
      return false;
    }
    if (list.datapoints[0].value != val) {
      LOG(INFO) << "expected value " << val << ", got " << list.datapoints[0].value;
      return false;
    }
    return true;
  }

  typedef std::pair<std::string, std::string> tag_t;
  typedef std::vector<tag_t> tags_t;
  tags_t make_tags(const std::string& key, const std::string& val) {
    tags_t ret;
    ret.push_back(tag_t(key, val));
    return ret;
  }
  tags_t make_tags(const std::string& key1, const std::string& val1,
      const std::string& key2, const std::string& val2) {
    tags_t ret;
    ret.push_back(tag_t(key1, val1));
    ret.push_back(tag_t(key2, val2));
    return ret;
  }
  tags_t make_tags(const std::string& key1, const std::string& val1,
      const std::string& key2, const std::string& val2,
      const std::string& key3, const std::string& val3) {
    tags_t ret;
    ret.push_back(tag_t(key1, val1));
    ret.push_back(tag_t(key2, val2));
    ret.push_back(tag_t(key3, val3));
    return ret;
  }
  tags_t make_tags(const std::string& key1, const std::string& val1,
      const std::string& key2, const std::string& val2,
      const std::string& key3, const std::string& val3,
      const std::string& key4, const std::string& val4) {
    tags_t ret;
    ret.push_back(tag_t(key1, val1));
    ret.push_back(tag_t(key2, val2));
    ret.push_back(tag_t(key3, val3));
    ret.push_back(tag_t(key4, val4));
    return ret;
  }

  bool check_with_tags(const std::string& data, double val, const tags_t& tags,
      const std::string& name = "hello") {
    LOG(INFO) << data;
    metrics::avro_metrics_map_t map;
    metrics::AvroEncoder::statsd_to_struct(NULL, NULL, data.data(), data.size(), map);
    if (map.size() != 1) {
      LOG(INFO) << "expected map size 1, got " << map.size();
      return false;
    }
    const metrics::ContainerMetrics& cm = map[container_id(UNKNOWN)];
    if (!metrics::AvroEncoder::empty(cm.without_custom_tags)) {
      LOG(INFO) << "expected empty without_custom_tags section";
      return false;
    }

    const metrics_schema::MetricList& list = cm.with_custom_tags.back();
    if (!check_datapoint(list, val, name)) {
      return false;
    }

    if (tags.size() != list.tags.size()) {
      LOG(INFO) << "expected " << tags.size() << " tags, got " << list.tags.size();
      return false;
    }
    for (size_t i = 0; i < tags.size(); ++i) {
      const tag_t& expect_tag = tags[i];
      const metrics_schema::Tag& got_tag = list.tags[i];
      if (expect_tag.first != got_tag.key) {
        LOG(INFO) << "for tag " << i << ", "
                  << "expected key " << expect_tag.first << " (size " << expect_tag.first.size()
                  << "), got " << got_tag.key << "size (" << got_tag.key.size() << ")";
        return false;
      }
      if (expect_tag.second != got_tag.value) {
        LOG(INFO) << "for tag " << i << ", "
                  << "expected value " << expect_tag.second << " (size " << expect_tag.second.size()
                  << "), got " << got_tag.value << " (size " << got_tag.value.size() << ")";
        return false;
      }
    }
    return true;
  }

  bool check_no_tags(const std::string& data, double val, const std::string& name = "hello") {
    LOG(INFO) << data;
    metrics::avro_metrics_map_t map;
    metrics::AvroEncoder::statsd_to_struct(NULL, NULL, data.data(), data.size(), map);
    if (map.size() != 1) {
      LOG(INFO) << "expected map size 1, got " << map.size();
      return false;
    }
    const metrics::ContainerMetrics& cm = map[container_id(UNKNOWN)];
    if (!cm.with_custom_tags.empty()) {
      LOG(INFO) << "expected empty with_custom_tags section, got " << map.size();
      return false;
    }

    const metrics_schema::MetricList& list = cm.without_custom_tags;
    if (!check_datapoint(list, val, name)) {
      return false;
    }

    if (!list.tags.empty()) {
      LOG(INFO) << "expected empty tags, got " << list.tags.size();
      return false;
    }
    return true;
  }


  bool eq(const std::string& key, const std::string& val, const metrics_schema::Tag& tag) {
    return key == tag.key && val == tag.value;
  }
  bool eq(const std::string& name, double val, const metrics_schema::Datapoint& d) {
    return name == d.name && val == d.value;
  }
  bool eq(const std::string& name, double val, int64_t time_ms, const metrics_schema::Datapoint& d) {
    return eq(name, val, d) && time_ms == d.time_ms;
  }
  bool eq(const metrics_schema::MetricList& a, const metrics_schema::MetricList& b) {
    if (a.topic != b.topic) {
      LOG(INFO) << "topic " << a.topic << " != " << b.topic;
      return false;
    }
    if (a.tags.size() != b.tags.size()) {
      LOG(INFO) << "tags " << a.tags.size() << " != " << b.tags.size();
      return false;
    }
    for (size_t i = 0; i < a.tags.size(); ++i) {
      if (a.tags[i].key != b.tags[i].key) {
        LOG(INFO) << "tag key " << a.tags[i].key << " != " << b.tags[i].key;
        return false;
      }
      if (a.tags[i].value != b.tags[i].value) {
        LOG(INFO) << "tag val " << a.tags[i].value << " != " << b.tags[i].value;
        return false;
      }
    }
    if (a.datapoints.size() != b.datapoints.size()) {
      LOG(INFO) << "datapoints " << a.datapoints.size() << " != " << b.datapoints.size();
      return false;
    }
    for (size_t i = 0; i < a.datapoints.size(); ++i) {
      if (a.datapoints[i].name != b.datapoints[i].name) {
        LOG(INFO) << "datapoints " << a.datapoints[i].name << " != " << b.datapoints[i].name;
        return false;
      }
      if (a.datapoints[i].time_ms != b.datapoints[i].time_ms) {
        LOG(INFO) << "datapoints " << a.datapoints[i].time_ms << " != " << b.datapoints[i].time_ms;
        return false;
      }
      if (a.datapoints[i].value != b.datapoints[i].value) {
        LOG(INFO) << "datapoints " << a.datapoints[i].value << " != " << b.datapoints[i].value;
        return false;
      }
    }
    return true;
  }

  void print_schema(const metrics_schema::MetricList& m) {
    LOG(INFO) << "topic " << m.topic;
    LOG(INFO) << "tags: ";
    for (const metrics_schema::Tag& t : m.tags) {
      LOG(INFO) << "  " << t.key << "=" << t.value;
    }
    LOG(INFO) << "points: ";
    for (const metrics_schema::Datapoint& d : m.datapoints) {
      LOG(INFO) << "  " << d.name << "=" << d.value << " @ " << d.time_ms;
    }
  }
}

class AvroEncoderTests : public ::testing::Test {
 protected:
  virtual void TearDown() {
    for (const std::string& tmpfile : tmpfiles) {
      LOG(INFO) << "Deleting tmp file: " << tmpfile;
      unlink(tmpfile.data());
    }
  }

  std::string write_tmp(const std::string& data = std::string()) {
    std::string tmpname("avro_encoder_tests-XXXXXX");
    int tmpfd = mkstemp((char*)tmpname.data());

    // get path to tmpfile
    std::ostringstream readpath;
    readpath << "/proc/self/fd/" << tmpfd;
    std::string buf(1024, '\0');
    int wrote = readlink(readpath.str().data(), (char*)buf.data(), buf.size());
    std::string tmppath = std::string(buf.data(), wrote);

    if (!data.empty()) {
      FILE* tmpfp = fdopen(tmpfd, "w");
      fwrite(data.data(), data.size(), 1, tmpfp);
      fclose(tmpfp);
    }

    LOG(INFO) << "Created tmp file (with " << data.size() << "b): " << tmppath;
    tmpfiles.push_back(tmppath);
    return tmppath;
  }

 private:
  std::vector<std::string> tmpfiles;
};

TEST_F(AvroEncoderTests, header) {
  const std::string header = metrics::AvroEncoder::header();
  const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);
  ASSERT_EQ(601, header.size());// just a check for consistency
  EXPECT_EQ('O', header[0]);
  EXPECT_EQ('b', header[1]);
  EXPECT_EQ('j', header[2]);
  EXPECT_EQ('\x01', header[3]);
}

TEST_F(AvroEncoderTests, encode_empty_metrics) {
  metrics::avro_metrics_map_t map;
  map[container_id("hello")].without_custom_tags = metrics_schema::MetricList();
  std::ostringstream oss;
  metrics::AvroEncoder::encode_metrics_block(map, oss);
  EXPECT_EQ(0, oss.str().size());

  const std::string header = metrics::AvroEncoder::header();
  const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);

  std::string tmppath = write_tmp(header);
  metrics_schema::MetricList list;

  std::ostringstream no_schema_reader_strm, no_schema_data_strm;
  {
    avro::DataFileReader<metrics_schema::MetricList> avro_reader_no_schema(tmppath.data());
    EXPECT_FALSE(avro_reader_no_schema.read(list));
    EXPECT_TRUE(list.topic.empty());
    EXPECT_TRUE(list.tags.empty());
    EXPECT_TRUE(list.datapoints.empty());

    avro_reader_no_schema.readerSchema().toJson(no_schema_reader_strm);
    avro_reader_no_schema.dataSchema().toJson(no_schema_data_strm);
  }

  std::ostringstream with_schema_reader_strm, with_schema_data_strm;
  {
    avro::DataFileReader<metrics_schema::MetricList> avro_reader_with_schema(
        tmppath.data(), schema);
    EXPECT_FALSE(avro_reader_with_schema.read(list));
    EXPECT_TRUE(list.topic.empty());
    EXPECT_TRUE(list.tags.empty());
    EXPECT_TRUE(list.datapoints.empty());

    avro_reader_with_schema.readerSchema().toJson(with_schema_reader_strm);
    avro_reader_with_schema.dataSchema().toJson(with_schema_data_strm);
  }

  EXPECT_EQ(no_schema_reader_strm.str(), no_schema_data_strm.str());
  EXPECT_EQ(with_schema_reader_strm.str(), with_schema_data_strm.str());
  EXPECT_EQ(no_schema_reader_strm.str(), with_schema_reader_strm.str());
  EXPECT_EQ(no_schema_data_strm.str(), with_schema_data_strm.str());
}

TEST_F(AvroEncoderTests, encode_one_metric_list) {
  metrics_schema::MetricList list = metric_list("tagged_metrics");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  list.datapoints.push_back(datapoint("pt2", 5, 3.8));
  list.datapoints.push_back(datapoint("pt3", 5, 3.8));
  list.tags.push_back(tag("k1", "v1"));
  list.tags.push_back(tag("k2", "v2"));
  list.tags.push_back(tag("k3", "v3"));

  {
    std::ostringstream oss;
    metrics::AvroEncoder::encode_metrics_block(to_map(list), oss);
    EXPECT_EQ(95, oss.str().size()); // just check for consistency
  }

  std::string tmppath = write_tmp();
  {
    std::ofstream ofs(tmppath, std::ios::binary);
    ofs << metrics::AvroEncoder::header(); // actual file must start with header
    metrics::AvroEncoder::encode_metrics_block(to_map(list), ofs);
  }
  {
    const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);
    avro::DataFileWriter<metrics_schema::MetricList> writer(write_tmp().data(), schema);
    writer.write(list);
  }

  avro::DataFileReader<metrics_schema::MetricList> avro_reader(tmppath.data());
  metrics_schema::MetricList flist;
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(list, flist));
  EXPECT_FALSE(avro_reader.read(flist));
}

TEST_F(AvroEncoderTests, encode_many_metrics) {
  metrics::avro_metrics_map_t map;
  map[container_id("empty")].without_custom_tags = metric_list("empty_topic");

  metrics_schema::MetricList list = metric_list("one_metric");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  map[container_id("one")].without_custom_tags = list;

  list = metric_list("tags_only");
  list.tags.push_back(tag("k1", "v1"));
  list.tags.push_back(tag("k2", "v2"));
  list.tags.push_back(tag("k3", "v3"));
  map[container_id("tags")].without_custom_tags = list;

  list = metric_list("many_metrics");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  list.datapoints.push_back(datapoint("pt2", 5, 3.8));
  list.datapoints.push_back(datapoint("pt3", 5, 3.8));
  map[container_id("many")].without_custom_tags = list;

  list = metric_list("zzztagged_metrics");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  list.datapoints.push_back(datapoint("pt2", 5, 3.8));
  list.datapoints.push_back(datapoint("pt3", 5, 3.8));
  list.tags.push_back(tag("k1", "v1"));
  list.tags.push_back(tag("k2", "v2"));
  list.tags.push_back(tag("k3", "v3"));
  map[container_id("many_tagged")].without_custom_tags = list;

  list = metric_list("tagged_container_stats");
  list.datapoints.push_back(datapoint("cpt1", 5, 3.8));
  list.datapoints.push_back(datapoint("cpt2", 5, 3.8));
  list.datapoints.push_back(datapoint("cpt3", 5, 3.8));
  list.tags.push_back(tag("ck1", "v1"));
  list.tags.push_back(tag("ck2", "v2"));
  list.tags.push_back(tag("ck3", "v3"));
  map[container_id("more_tagged")].without_custom_tags = list;

  {
    std::ostringstream oss;
    metrics::AvroEncoder::encode_metrics_block(map, oss);
    EXPECT_EQ(315, oss.str().size()); // just check for consistency
  }

  std::string tmppath = write_tmp();
  {
    std::ofstream ofs(tmppath, std::ios::binary);
    ofs << metrics::AvroEncoder::header(); // actual file must start with header
    metrics::AvroEncoder::encode_metrics_block(map, ofs);
  }

  avro::DataFileReader<metrics_schema::MetricList> avro_reader(tmppath.data());
  metrics_schema::MetricList flist;
  // ordering: map entries ordered by map key, followed by the standalone list
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("empty")].without_custom_tags, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("many")].without_custom_tags, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("many_tagged")].without_custom_tags, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("more_tagged")].without_custom_tags, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("one")].without_custom_tags, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("tags")].without_custom_tags, flist));
  EXPECT_FALSE(avro_reader.read(flist));
}

TEST_F(AvroEncoderTests, encode_many_blocks) {
  metrics_schema::MetricList empty;
  metrics_schema::MetricList topic = metric_list("topic");

  metrics_schema::MetricList one = metric_list("one_metric");
  one.datapoints.push_back(datapoint("pt1", 5, 3.8));

  metrics_schema::MetricList tags = metric_list("tags_only");
  tags.tags.push_back(tag("k1", "v1"));
  tags.tags.push_back(tag("k2", "v2"));
  tags.tags.push_back(tag("k3", "v3"));

  metrics_schema::MetricList untagged = metric_list("many_metrics");
  untagged.datapoints.push_back(datapoint("pt1", 5, 3.8));
  untagged.datapoints.push_back(datapoint("pt2", 5, 3.8));
  untagged.datapoints.push_back(datapoint("pt3", 5, 3.8));

  metrics_schema::MetricList taggeda = metric_list("tagged_metrics");
  taggeda.datapoints.push_back(datapoint("pt1", 5, 3.8));
  taggeda.datapoints.push_back(datapoint("pt2", 5, 3.8));
  taggeda.datapoints.push_back(datapoint("pt3", 5, 3.8));
  taggeda.tags.push_back(tag("k1", "v1"));
  taggeda.tags.push_back(tag("k2", "v2"));
  taggeda.tags.push_back(tag("k3", "v3"));

  metrics_schema::MetricList taggedb = metric_list("tagged_container_statsb");
  taggedb.datapoints.push_back(datapoint("bpt1", 5, 3.8));
  taggedb.datapoints.push_back(datapoint("bpt2", 5, 3.8));
  taggedb.datapoints.push_back(datapoint("bpt3", 5, 3.8));
  taggedb.tags.push_back(tag("bk1", "v1"));
  taggedb.tags.push_back(tag("bk2", "v2"));
  taggedb.tags.push_back(tag("bk3", "v3"));

  metrics_schema::MetricList taggedc = metric_list("tagged_container_statsc");
  taggedc.datapoints.push_back(datapoint("cpt1", 5, 3.8));
  taggedc.datapoints.push_back(datapoint("cpt2", 5, 3.8));
  taggedc.datapoints.push_back(datapoint("cpt3", 5, 3.8));
  taggedc.tags.push_back(tag("ck1", "v1"));
  taggedc.tags.push_back(tag("ck2", "v2"));
  taggedc.tags.push_back(tag("ck3", "v3"));

  std::string tmppath = write_tmp();
  {
    std::ofstream ofs(tmppath, std::ios::binary);
    ofs << metrics::AvroEncoder::header(); // actual file must start with header
    metrics::AvroEncoder::encode_metrics_block(merge(to_map(empty),to_map(topic)), ofs);
    metrics::AvroEncoder::encode_metrics_block(to_map(one), ofs);
    metrics::AvroEncoder::encode_metrics_block(metrics::avro_metrics_map_t(), ofs);
    metrics::AvroEncoder::encode_metrics_block(to_map(tags), ofs);
    metrics::AvroEncoder::encode_metrics_block(to_map(empty), ofs);
    metrics::AvroEncoder::encode_metrics_block(to_map(untagged), ofs);
    metrics::AvroEncoder::encode_metrics_block(merge(to_map(taggeda),to_map(taggedb)), ofs);
    metrics::AvroEncoder::encode_metrics_block(to_map(taggedc), ofs);
  }

  avro::DataFileReader<metrics_schema::MetricList> avro_reader(tmppath.data());
  metrics_schema::MetricList flist;
  // ordering: map entries ordered by map key, followed by the standalone list
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(topic, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(one, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(tags, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(untagged, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(taggedb, flist)); // in merged map with taggeda, this comes first
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(taggeda, flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(taggedc, flist));
  EXPECT_FALSE(avro_reader.read(flist));
}

TEST_F(AvroEncoderTests, map_no_info) {
  metrics::avro_metrics_map_t map;
  metrics::AvroEncoder::statsd_to_struct(NULL, NULL, "hello", 5, map);
  EXPECT_EQ(1, map.size());
  const metrics_schema::MetricList& list = map[container_id(UNKNOWN)].without_custom_tags;
  EXPECT_EQ(UNKNOWN, list.topic);
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_EQ("hello", list.datapoints[0].name);
  EXPECT_NE(0, list.datapoints[0].time_ms);
  EXPECT_EQ(0, list.datapoints[0].value);
  EXPECT_TRUE(list.tags.empty());
}

TEST_F(AvroEncoderTests, map_with_info) {
  mesos::ContainerID cid = container_id("cid");
  mesos::ExecutorInfo einfo = exec_info("fid", "eid");
  metrics::avro_metrics_map_t map;
  metrics::AvroEncoder::statsd_to_struct(&cid, &einfo, "hello", 5, map);
  EXPECT_EQ(1, map.size());
  const metrics_schema::MetricList& list = map[container_id("cid")].without_custom_tags;
  EXPECT_EQ("fid", list.topic);
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_EQ("hello", list.datapoints[0].name);
  EXPECT_NE(0, list.datapoints[0].time_ms);
  EXPECT_EQ(0, list.datapoints[0].value);
  EXPECT_EQ(3, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid", list.tags[2]));
}

TEST_F(AvroEncoderTests, statsd_merge) {
  mesos::ContainerID cid = container_id("cid");

  metrics::avro_metrics_map_t map;
  metrics_schema::MetricList& preinit_list = map[cid].without_custom_tags;
  preinit_list.topic = "testt";
  metrics_schema::Tag tag;
  tag.key = "testk";
  tag.value = "valuek";
  preinit_list.tags.push_back(tag);
  tag.key = "container_id";
  tag.value = "testc";
  preinit_list.tags.push_back(tag);
  metrics_schema::Datapoint d;
  d.name = "testn";
  d.value = 10.3;
  d.time_ms = 123;
  preinit_list.datapoints.push_back(d);

  mesos::ExecutorInfo einfo = exec_info("fid", "eid");
  std::string stat("hello:3.8");
  metrics::AvroEncoder::statsd_to_struct(&cid, &einfo, stat.data(), stat.size(), map);
  EXPECT_EQ(1, map.size());

  EXPECT_EQ("testt", preinit_list.topic);// original topic left intact

  EXPECT_EQ(4, preinit_list.tags.size());
  EXPECT_TRUE(eq("testk", "valuek", preinit_list.tags[0]));
  EXPECT_TRUE(eq("container_id", "testc", preinit_list.tags[1]));// original tag left intact
  EXPECT_TRUE(eq("framework_id", "fid", preinit_list.tags[2]));
  EXPECT_TRUE(eq("executor_id", "eid", preinit_list.tags[3]));

  EXPECT_EQ(2, preinit_list.datapoints.size());
  EXPECT_TRUE(eq("testn", 10.3, 123, preinit_list.datapoints[0]));
  EXPECT_TRUE(eq("hello", 3.8, preinit_list.datapoints[1]));
}

TEST_F(AvroEncoderTests, statsd_parse_mixed_vals) {
  std::string
    untagged_1("v1:1"),
    untagged_2("v2:2"),
    untagged_3("v3:3"),
    tagged_a1("v10:10|#ta:va"),
    tagged_a2("v11:11|#ta:va"),
    tagged_b1("v12:12|#tb:vb"),
    tagged_b2("v13:13|#tb:vb,tx:vx");

  metrics::avro_metrics_map_t map;

  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, untagged_1.data(), untagged_1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, untagged_2.data(), untagged_2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, untagged_3.data(), untagged_3.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, tagged_a1.data(), tagged_a1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, tagged_a2.data(), tagged_a2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, tagged_b1.data(), tagged_b1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          NULL, NULL, tagged_b2.data(), tagged_b2.size(), map));

  mesos::ContainerID cid1 = container_id("cid1");
  mesos::ExecutorInfo einfo1 = exec_info("fid1", "eid1");
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, tagged_a1.data(), tagged_a1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, untagged_1.data(), untagged_1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, tagged_a2.data(), tagged_a2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, untagged_2.data(), untagged_2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, tagged_b1.data(), tagged_b1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, untagged_3.data(), untagged_3.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid1, &einfo1, tagged_b2.data(), tagged_b2.size(), map));

  mesos::ContainerID cid2 = container_id("cid2");
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, tagged_a1.data(), tagged_a1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, tagged_a2.data(), tagged_a2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, tagged_b1.data(), tagged_b1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, tagged_b2.data(), tagged_b2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, untagged_1.data(), untagged_1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, untagged_2.data(), untagged_2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid2, &einfo1, untagged_3.data(), untagged_3.size(), map));

  mesos::ContainerID cid3 = container_id("cid3");
  mesos::ExecutorInfo einfo2 = exec_info("fid2", "eid2");
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, untagged_1.data(), untagged_1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, tagged_a1.data(), tagged_a1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, untagged_2.data(), untagged_2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, tagged_a2.data(), tagged_a2.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, untagged_3.data(), untagged_3.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, tagged_b1.data(), tagged_b1.size(), map));
  EXPECT_EQ(1, metrics::AvroEncoder::statsd_to_struct(
          &cid3, &einfo2, tagged_b2.data(), tagged_b2.size(), map));

  EXPECT_EQ(4, map.size()); // unknown + cid[1-3]

  // UNKNOWN

  metrics::ContainerMetrics& cm = map[container_id(UNKNOWN)];
  metrics_schema::MetricList& list = cm.without_custom_tags;
  EXPECT_EQ(UNKNOWN, list.topic);
  EXPECT_EQ(0, list.tags.size());
  EXPECT_EQ(3, list.datapoints.size());
  EXPECT_TRUE(eq("v1", 1, list.datapoints[0]));
  EXPECT_TRUE(eq("v2", 2, list.datapoints[1]));
  EXPECT_TRUE(eq("v3", 3, list.datapoints[2]));

  EXPECT_EQ(4, cm.with_custom_tags.size());
  list = cm.with_custom_tags[0];
  EXPECT_EQ(UNKNOWN, list.topic);
  EXPECT_EQ(1, list.tags.size());
  EXPECT_TRUE(eq("ta", "va", list.tags[0]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v10", 10, list.datapoints[0]));
  list = cm.with_custom_tags[1];
  EXPECT_EQ(UNKNOWN, list.topic);
  EXPECT_EQ(1, list.tags.size());
  EXPECT_TRUE(eq("ta", "va", list.tags[0]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v11", 11, list.datapoints[0]));
  list = cm.with_custom_tags[2];
  EXPECT_EQ(UNKNOWN, list.topic);
  EXPECT_EQ(1, list.tags.size());
  EXPECT_TRUE(eq("tb", "vb", list.tags[0]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v12", 12, list.datapoints[0]));
  list = cm.with_custom_tags[3];
  EXPECT_EQ(UNKNOWN, list.topic);
  EXPECT_EQ(2, list.tags.size());
  EXPECT_TRUE(eq("tb", "vb", list.tags[0]));
  EXPECT_TRUE(eq("tx", "vx", list.tags[1]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v13", 13, list.datapoints[0]));

  // cid1

  cm = map[container_id("cid1")];
  list = cm.without_custom_tags;
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(3, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid1", list.tags[2]));
  EXPECT_EQ(3, list.datapoints.size());
  EXPECT_TRUE(eq("v1", 1, list.datapoints[0]));
  EXPECT_TRUE(eq("v2", 2, list.datapoints[1]));
  EXPECT_TRUE(eq("v3", 3, list.datapoints[2]));

  EXPECT_EQ(4, cm.with_custom_tags.size());
  list = cm.with_custom_tags[0];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid1", list.tags[2]));
  EXPECT_TRUE(eq("ta", "va", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v10", 10, list.datapoints[0]));
  list = cm.with_custom_tags[1];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid1", list.tags[2]));
  EXPECT_TRUE(eq("ta", "va", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v11", 11, list.datapoints[0]));
  list = cm.with_custom_tags[2];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid1", list.tags[2]));
  EXPECT_TRUE(eq("tb", "vb", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v12", 12, list.datapoints[0]));
  list = cm.with_custom_tags[3];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(5, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid1", list.tags[2]));
  EXPECT_TRUE(eq("tb", "vb", list.tags[3]));
  EXPECT_TRUE(eq("tx", "vx", list.tags[4]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v13", 13, list.datapoints[0]));

  // cid2

  cm = map[container_id("cid2")];
  list = cm.without_custom_tags;
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(3, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid2", list.tags[2]));
  EXPECT_EQ(3, list.datapoints.size());
  EXPECT_TRUE(eq("v1", 1, list.datapoints[0]));
  EXPECT_TRUE(eq("v2", 2, list.datapoints[1]));
  EXPECT_TRUE(eq("v3", 3, list.datapoints[2]));

  EXPECT_EQ(4, cm.with_custom_tags.size());
  list = cm.with_custom_tags[0];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid2", list.tags[2]));
  EXPECT_TRUE(eq("ta", "va", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v10", 10, list.datapoints[0]));
  list = cm.with_custom_tags[1];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid2", list.tags[2]));
  EXPECT_TRUE(eq("ta", "va", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v11", 11, list.datapoints[0]));
  list = cm.with_custom_tags[2];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid2", list.tags[2]));
  EXPECT_TRUE(eq("tb", "vb", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v12", 12, list.datapoints[0]));
  list = cm.with_custom_tags[3];
  EXPECT_EQ("fid1", list.topic);
  EXPECT_EQ(5, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid1", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid1", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid2", list.tags[2]));
  EXPECT_TRUE(eq("tb", "vb", list.tags[3]));
  EXPECT_TRUE(eq("tx", "vx", list.tags[4]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v13", 13, list.datapoints[0]));

  // cid3

  cm = map[container_id("cid3")];
  list = cm.without_custom_tags;
  EXPECT_EQ("fid2", list.topic);
  EXPECT_EQ(3, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid2", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid2", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid3", list.tags[2]));
  EXPECT_EQ(3, list.datapoints.size());
  EXPECT_TRUE(eq("v1", 1, list.datapoints[0]));
  EXPECT_TRUE(eq("v2", 2, list.datapoints[1]));
  EXPECT_TRUE(eq("v3", 3, list.datapoints[2]));

  EXPECT_EQ(4, cm.with_custom_tags.size());
  list = cm.with_custom_tags[0];
  EXPECT_EQ("fid2", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid2", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid2", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid3", list.tags[2]));
  EXPECT_TRUE(eq("ta", "va", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v10", 10, list.datapoints[0]));
  list = cm.with_custom_tags[1];
  EXPECT_EQ("fid2", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid2", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid2", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid3", list.tags[2]));
  EXPECT_TRUE(eq("ta", "va", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v11", 11, list.datapoints[0]));
  list = cm.with_custom_tags[2];
  EXPECT_EQ("fid2", list.topic);
  EXPECT_EQ(4, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid2", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid2", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid3", list.tags[2]));
  EXPECT_TRUE(eq("tb", "vb", list.tags[3]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v12", 12, list.datapoints[0]));
  list = cm.with_custom_tags[3];
  EXPECT_EQ("fid2", list.topic);
  EXPECT_EQ(5, list.tags.size());
  EXPECT_TRUE(eq("framework_id", "fid2", list.tags[0]));
  EXPECT_TRUE(eq("executor_id", "eid2", list.tags[1]));
  EXPECT_TRUE(eq("container_id", "cid3", list.tags[2]));
  EXPECT_TRUE(eq("tb", "vb", list.tags[3]));
  EXPECT_TRUE(eq("tx", "vx", list.tags[4]));
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_TRUE(eq("v13", 13, list.datapoints[0]));

  for (auto iter = map.begin(); iter != map.end(); ++iter) {
    LOG(INFO) << "----- " << iter->first.DebugString();
    print_schema(iter->second.without_custom_tags);
    for (auto iterb = iter->second.with_custom_tags.begin();
         iterb != iter->second.with_custom_tags.end();
         ++iterb) {
      LOG(INFO) << "---";
      print_schema(*iterb);
    }
  }
}

TEST_F(AvroEncoderTests, statsd_parse_single) {
  std::string
    empty_1tag("|#tag:val"),
    eempty_1tag(":|#tag:val"),
    hello_noval("hello"),
    hello_noeval("hello:"),
    hello_spaceval("hello: "),
    hello_wordval("hello:hi"),
    hello_noval_1tag("hello|#tag:val"),
    hello_noval_1tag_noval("hello|#tag"),
    hello_spaceval_1tag("hello: |#tag:val"),
    hello_wordval_1tag("hello:hi|#tag:val");

  EXPECT_TRUE(check_with_tags(empty_1tag, 0, make_tags("tag", "val"), ""));
  EXPECT_TRUE(check_with_tags(eempty_1tag, 0, make_tags("tag", "val"), ""));
  EXPECT_TRUE(check_no_tags(hello_noval, 0));
  EXPECT_TRUE(check_no_tags(hello_noeval, 0));
  EXPECT_TRUE(check_no_tags(hello_spaceval, 0));
  EXPECT_TRUE(check_no_tags(hello_wordval, 0));
  EXPECT_TRUE(check_with_tags(hello_noval_1tag, 0, make_tags("tag", "val")));
  EXPECT_TRUE(check_with_tags(hello_noval_1tag_noval, 0, make_tags("tag", "")));
  EXPECT_TRUE(check_with_tags(hello_spaceval_1tag, 0, make_tags("tag", "val")));
  EXPECT_TRUE(check_with_tags(hello_wordval_1tag, 0, make_tags("tag", "val")));

  std::string
    hello_noval_2endtag("hello|@0.5|#tag:val"),
    hello_noval_2starttag("hello|#tag:val|@0.5");

  EXPECT_TRUE(check_with_tags(hello_noval_2endtag, 0, make_tags("tag", "val")));
  EXPECT_TRUE(check_with_tags(hello_noval_2starttag, 0, make_tags("tag", "val")));

  std::string
    hello_noval_2endtag_noval("hello|@0.5|#tag"),
    hello_noval_2starttag_noval("hello|#ta|@0.5");

  EXPECT_TRUE(check_with_tags(hello_noval_2endtag_noval, 0, make_tags("tag", "")));
  EXPECT_TRUE(check_with_tags(hello_noval_2starttag_noval, 0, make_tags("ta", "")));

  std::string
    hello_notag("hello:0.35"),
    hello_1tag("hello:0.35|#tag");

  EXPECT_TRUE(check_no_tags(hello_notag, 0.35));
  EXPECT_TRUE(check_with_tags(hello_1tag, 0.35, make_tags("tag", "")));

  std::string
    hello_2endtag("hello:0.35|@0.5|#tag:val"),
    hello_2starttag("hello:0.35|#ta:val|@0.5"),
    hello_2starttag_zerosampling("hello:0.35|#ta:val|@0");

  EXPECT_TRUE(check_with_tags(hello_2endtag, 0.7, make_tags("tag", "val")));
  EXPECT_TRUE(check_with_tags(hello_2starttag, 0.7, make_tags("ta", "val")));
  EXPECT_TRUE(check_with_tags(hello_2starttag_zerosampling, 0.35, make_tags("ta", "val")));

  std::string
    hello_3endtag("hello:0.35|&other|@0.5|#tag:val,t2:v2"),
    hello_3midtag("hello:0.35|&other|#tag:val,t2:v2|@0.5"),
    hello_3midtag_zerosampling("hello:0.35|&other|#tag:val,t2:v2|@0.0"),
    hello_3starttag("hello:0.35|#t:v,t2:v2|&other|@0.5");

  EXPECT_TRUE(check_with_tags(hello_3endtag, 0.7, make_tags("tag", "val", "t2", "v2")));
  EXPECT_TRUE(check_with_tags(hello_3midtag, 0.7, make_tags("tag", "val", "t2", "v2")));
  EXPECT_TRUE(check_with_tags(hello_3midtag_zerosampling, 0.35, make_tags("tag", "val", "t2", "v2")));
  EXPECT_TRUE(check_with_tags(hello_3starttag, 0.7, make_tags("t", "v", "t2", "v2")));

  std::string
    hello_3endtag_split("hello:0.35|#tag:val|&other|@0.5|#t2:v2"),
    hello_3midtag_split("hello:0.35|&other|#t2:v2|@0.5|#tag:val"),
    hello_3midtag_zerosampling_split("hello:0.35|&other|#tag:val,t2:v2|@0.0|#tag:val"),
    hello_3starttag_split("hello:0.35|#t:v,t2:v2|&other|#tag:val,t2:v2|@0.5");

  EXPECT_TRUE(check_with_tags(hello_3endtag_split, 0.7, make_tags("tag", "val", "t2", "v2")));
  EXPECT_TRUE(check_with_tags(hello_3midtag_split, 0.7, make_tags("t2", "v2", "tag", "val")));
  EXPECT_TRUE(check_with_tags(hello_3midtag_zerosampling_split, 0.35,
          make_tags("tag", "val", "t2", "v2", "tag", "val")));
  EXPECT_TRUE(check_with_tags(hello_3starttag_split, 0.7,
          make_tags("t", "v", "t2", "v2", "tag", "val", "t2", "v2")));

  // corrupt/weird data
  std::string
    hello_1emptytag("hello:0.35|#"),
    hello_1emptytagval("hello:0.35|#,"),
    hello_1emptytageval("hello:0.35|#:,:"),
    hello_1emptyend("hello:0.35|"),

    hello_2tag_empty("hello:0.35|#tag1|"),
    hello_2tag_emptyeval("hello:0.35|#tag1:|"),
    hello_2tag_emptyval("hello:0.35|#tag1:val|"),
    hello_2empty_empty("hello:0.35||"),

    hello_2empty_tag("hello:0.35||#tag1"),
    hello_2empty_tageval("hello:0.35||#tag1:"),
    hello_2empty_tagval("hello:0.35||#tag1:val"),
    hello_2tag_tag("hello:0.35|#tag1|#tag2"),

    hello_2tag_tageval("hello:0.35|#tag1|#tag2:"),
    hello_2tag_tagval("hello:0.35|#tag1|#tag2:val"),
    hello_2empty_emptytag("hello:0.35||#"),
    hello_2empty_emptytagval("hello:0.35||#,"),

    hello_2empty_emptytageval("hello:0.35||#:,:"),
    hello_2emptytag_empty("hello:0.35|#|"),
    hello_2emptytagval_empty("hello:0.35|#,|"),
    hello_2emptytageval_empty("hello:0.35|#:,:|");

  EXPECT_TRUE(check_no_tags(hello_1emptytag, 0.35));
  EXPECT_TRUE(check_no_tags(hello_1emptytagval, 0.35));
  EXPECT_TRUE(check_no_tags(hello_1emptytageval, 0.35));
  EXPECT_TRUE(check_no_tags(hello_1emptyend, 0.35));

  EXPECT_TRUE(check_with_tags(hello_2tag_empty, 0.35, make_tags("tag1", "")));
  EXPECT_TRUE(check_with_tags(hello_2tag_emptyeval, 0.35, make_tags("tag1", "")));
  EXPECT_TRUE(check_with_tags(hello_2tag_emptyval, 0.35, make_tags("tag1", "val")));
  EXPECT_TRUE(check_no_tags(hello_2empty_empty, 0.35));

  EXPECT_TRUE(check_with_tags(hello_2empty_tag, 0.35, make_tags("tag1", "")));
  EXPECT_TRUE(check_with_tags(hello_2empty_tageval, 0.35, make_tags("tag1", "")));
  EXPECT_TRUE(check_with_tags(hello_2empty_tagval, 0.35, make_tags("tag1", "val")));
  EXPECT_TRUE(check_with_tags(hello_2tag_tag, 0.35, make_tags("tag1", "", "tag2", "")));

  EXPECT_TRUE(check_with_tags(hello_2tag_tageval, 0.35, make_tags("tag1", "", "tag2", "")));
  EXPECT_TRUE(check_with_tags(hello_2tag_tagval, 0.35, make_tags("tag1", "", "tag2", "val")));
  EXPECT_TRUE(check_no_tags(hello_2empty_emptytag, 0.35));
  EXPECT_TRUE(check_no_tags(hello_2empty_emptytagval, 0.35));

  EXPECT_TRUE(check_no_tags(hello_2empty_emptytageval, 0.35));
  EXPECT_TRUE(check_no_tags(hello_2emptytag_empty, 0.35));
  EXPECT_TRUE(check_no_tags(hello_2emptytagval_empty, 0.35));
  EXPECT_TRUE(check_no_tags(hello_2emptytageval_empty, 0.35));
}

TEST_F(AvroEncoderTests, resources) {
  mesos::ResourceUsage usage;

  mesos::ResourceUsage_Executor* executor1 = usage.add_executors();
  executor1->mutable_container_id()->set_value("cid1");
  executor1->mutable_executor_info()->mutable_framework_id()->set_value("fid1");
  executor1->mutable_executor_info()->mutable_executor_id()->set_value("eid1");
  mesos::ResourceStatistics* stats1 = executor1->mutable_statistics();
  stats1->set_timestamp(1234.55);
  stats1->set_processes(3);
  stats1->mutable_perf()->set_cpu_clock(0.7);
  stats1->set_net_rx_bytes(5);
  mesos::TrafficControlStatistics* traf1a = stats1->add_net_traffic_control_statistics();
  traf1a->set_id("1a");
  traf1a->set_bytes(1248);
  mesos::TrafficControlStatistics* traf1b = stats1->add_net_traffic_control_statistics();
  traf1b->set_id("1b");
  traf1b->set_ratepps(14);
  stats1->mutable_net_snmp_statistics()->mutable_ip_stats()->set_indelivers(123);
  stats1->mutable_net_snmp_statistics()->mutable_icmp_stats()->set_outsrcquenchs(481);
  stats1->mutable_net_snmp_statistics()->mutable_tcp_stats()->set_retranssegs(361);
  stats1->mutable_net_snmp_statistics()->mutable_udp_stats()->set_inerrors(8);

  mesos::ResourceUsage_Executor* executor2 = usage.add_executors();
  executor2->mutable_container_id()->set_value("cid2");
  executor2->mutable_executor_info()->mutable_framework_id()->set_value("fid2");
  executor2->mutable_executor_info()->mutable_executor_id()->set_value("eid2");
  mesos::ResourceStatistics* stats2 = executor2->mutable_statistics();
  stats2->set_timestamp(5678.99);
  stats2->set_mem_total_bytes(8);
  stats2->set_net_tx_dropped(6);
  stats2->mutable_net_snmp_statistics()->mutable_ip_stats()->set_outnoroutes(8);

  metrics::avro_metrics_map_t metric_map;
  EXPECT_EQ(12, metrics::AvroEncoder::resources_to_struct(usage, metric_map));
  EXPECT_EQ(2, metric_map.size());
  const metrics_schema::MetricList& m1 = metric_map[container_id("cid1")].without_custom_tags;

  EXPECT_EQ("fid1", m1.topic);

  EXPECT_EQ(3, m1.tags.size());
  EXPECT_EQ("framework_id", m1.tags[0].key);
  EXPECT_EQ("fid1", m1.tags[0].value);
  EXPECT_EQ("executor_id", m1.tags[1].key);
  EXPECT_EQ("eid1", m1.tags[1].value);
  EXPECT_EQ("container_id", m1.tags[2].key);
  EXPECT_EQ("cid1", m1.tags[2].value);

  EXPECT_EQ(9, m1.datapoints.size());
  for (const metrics_schema::Datapoint& d : m1.datapoints) {
    EXPECT_EQ(stats1->timestamp() * 1000, d.time_ms);
  }
  EXPECT_EQ("usage.processes", m1.datapoints[0].name);
  EXPECT_EQ(stats1->processes(), m1.datapoints[0].value);
  EXPECT_EQ("perf.cpu_clock", m1.datapoints[1].name);
  EXPECT_EQ(stats1->perf().cpu_clock(), m1.datapoints[1].value);
  EXPECT_EQ("usage.net_rx_bytes", m1.datapoints[2].name);
  EXPECT_EQ(stats1->net_rx_bytes(), m1.datapoints[2].value);
  EXPECT_EQ("traf.1a.bytes", m1.datapoints[3].name);
  EXPECT_EQ(traf1a->bytes(), m1.datapoints[3].value);
  EXPECT_EQ("traf.1b.ratepps", m1.datapoints[4].name);
  EXPECT_EQ(traf1b->ratepps(), m1.datapoints[4].value);
  EXPECT_EQ("snmp.ip.indelivers", m1.datapoints[5].name);
  EXPECT_EQ(stats1->net_snmp_statistics().ip_stats().indelivers(), m1.datapoints[5].value);
  EXPECT_EQ("snmp.icmp.outsrcquenchs", m1.datapoints[6].name);
  EXPECT_EQ(stats1->net_snmp_statistics().icmp_stats().outsrcquenchs(), m1.datapoints[6].value);
  EXPECT_EQ("snmp.tcp.retranssegs", m1.datapoints[7].name);
  EXPECT_EQ(stats1->net_snmp_statistics().tcp_stats().retranssegs(), m1.datapoints[7].value);
  EXPECT_EQ("snmp.udp.inerrors", m1.datapoints[8].name);
  EXPECT_EQ(stats1->net_snmp_statistics().udp_stats().inerrors(), m1.datapoints[8].value);

  const metrics_schema::MetricList& m2 = metric_map[container_id("cid2")].without_custom_tags;
  EXPECT_EQ("fid2", m2.topic);

  EXPECT_EQ(3, m2.tags.size());
  EXPECT_EQ("framework_id", m2.tags[0].key);
  EXPECT_EQ("fid2", m2.tags[0].value);
  EXPECT_EQ("executor_id", m2.tags[1].key);
  EXPECT_EQ("eid2", m2.tags[1].value);
  EXPECT_EQ("container_id", m2.tags[2].key);
  EXPECT_EQ("cid2", m2.tags[2].value);

  EXPECT_EQ(3, m2.datapoints.size());
  for (const metrics_schema::Datapoint& d : m2.datapoints) {
    EXPECT_EQ(stats2->timestamp() * 1000, d.time_ms);
  }
  EXPECT_EQ("usage.mem_total_bytes", m2.datapoints[0].name);
  EXPECT_EQ(stats2->mem_total_bytes(), m2.datapoints[0].value);
  EXPECT_EQ("usage.net_tx_dropped", m2.datapoints[1].name);
  EXPECT_EQ(stats2->net_tx_dropped(), m2.datapoints[1].value);
  EXPECT_EQ("snmp.ip.outnoroutes", m2.datapoints[2].name);
  EXPECT_EQ(stats2->net_snmp_statistics().ip_stats().outnoroutes(), m2.datapoints[2].value);
}

TEST_F(AvroEncoderTests, check_empty) {
  metrics_schema::MetricList list;
  EXPECT_TRUE(metrics::AvroEncoder::empty(list));

  list.topic = "hi";
  EXPECT_FALSE(metrics::AvroEncoder::empty(list));
  list.topic.clear();
  EXPECT_TRUE(metrics::AvroEncoder::empty(list));

  list.tags.push_back(metrics_schema::Tag());
  EXPECT_FALSE(metrics::AvroEncoder::empty(list));
  list.tags.clear();
  EXPECT_TRUE(metrics::AvroEncoder::empty(list));

  list.datapoints.push_back(metrics_schema::Datapoint());
  EXPECT_FALSE(metrics::AvroEncoder::empty(list));
  list.datapoints.clear();
  EXPECT_TRUE(metrics::AvroEncoder::empty(list));
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
