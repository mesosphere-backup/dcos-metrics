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
  inline metrics::container_id_ord_map<metrics_schema::MetricList> to_map(
      const metrics_schema::MetricList& list) {
    metrics::container_id_ord_map<metrics_schema::MetricList> map;
    map[container_id(list.topic)] = list;
    return map;
  }
  inline metrics::container_id_ord_map<metrics_schema::MetricList> merge(
      const metrics::container_id_ord_map<metrics_schema::MetricList>& a,
      const metrics::container_id_ord_map<metrics_schema::MetricList>& b) {
    metrics::container_id_ord_map<metrics_schema::MetricList> ret;
    ret.insert(a.begin(), a.end());
    ret.insert(b.begin(), b.end());
    return ret;
  }

  void check_no_tags(const std::string& data, double val, const std::string& name = "hello") {
    LOG(INFO) << data;
    metrics::container_id_ord_map<metrics_schema::MetricList> map;
    metrics::AvroEncoder::statsd_to_struct(NULL, NULL, data.data(), data.size(), map);
    EXPECT_EQ(1, map.size());
    const metrics_schema::MetricList& list = map[container_id(UNKNOWN)];
    EXPECT_EQ(UNKNOWN, list.topic);
    EXPECT_EQ(1, list.datapoints.size());
    EXPECT_EQ(name, list.datapoints[0].name);
    EXPECT_NE(0, list.datapoints[0].time_ms);
    EXPECT_EQ(val, list.datapoints[0].value);
    EXPECT_TRUE(list.tags.empty());
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
  metrics::container_id_ord_map<metrics_schema::MetricList> map;
  map[container_id("hello")] = metrics_schema::MetricList();
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
  metrics::container_id_ord_map<metrics_schema::MetricList> map;
  map[container_id("empty")] = metric_list("empty_topic");

  metrics_schema::MetricList list = metric_list("one_metric");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  map[container_id("one")] = list;

  list = metric_list("tags_only");
  list.tags.push_back(tag("k1", "v1"));
  list.tags.push_back(tag("k2", "v2"));
  list.tags.push_back(tag("k3", "v3"));
  map[container_id("tags")] = list;

  list = metric_list("many_metrics");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  list.datapoints.push_back(datapoint("pt2", 5, 3.8));
  list.datapoints.push_back(datapoint("pt3", 5, 3.8));
  map[container_id("many")] = list;

  list = metric_list("zzztagged_metrics");
  list.datapoints.push_back(datapoint("pt1", 5, 3.8));
  list.datapoints.push_back(datapoint("pt2", 5, 3.8));
  list.datapoints.push_back(datapoint("pt3", 5, 3.8));
  list.tags.push_back(tag("k1", "v1"));
  list.tags.push_back(tag("k2", "v2"));
  list.tags.push_back(tag("k3", "v3"));
  map[container_id("many_tagged")] = list;

  list = metric_list("tagged_container_stats");
  list.datapoints.push_back(datapoint("cpt1", 5, 3.8));
  list.datapoints.push_back(datapoint("cpt2", 5, 3.8));
  list.datapoints.push_back(datapoint("cpt3", 5, 3.8));
  list.tags.push_back(tag("ck1", "v1"));
  list.tags.push_back(tag("ck2", "v2"));
  list.tags.push_back(tag("ck3", "v3"));
  map[container_id("more_tagged")] = list;

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
  EXPECT_TRUE(eq(map[container_id("empty")], flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("many")], flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("many_tagged")], flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("more_tagged")], flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("one")], flist));
  EXPECT_TRUE(avro_reader.read(flist));
  EXPECT_TRUE(eq(map[container_id("tags")], flist));
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
    metrics::AvroEncoder::encode_metrics_block(
        metrics::container_id_ord_map<metrics_schema::MetricList>(), ofs);
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
  metrics::container_id_ord_map<metrics_schema::MetricList> map;
  metrics::AvroEncoder::statsd_to_struct(NULL, NULL, "hello", 5, map);
  EXPECT_EQ(1, map.size());
  const metrics_schema::MetricList& list = map[container_id(UNKNOWN)];
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
  metrics::container_id_ord_map<metrics_schema::MetricList> map;
  metrics::AvroEncoder::statsd_to_struct(&cid, &einfo, "hello", 5, map);
  EXPECT_EQ(1, map.size());
  const metrics_schema::MetricList& list = map[container_id("cid")];
  EXPECT_EQ("fid", list.topic);
  EXPECT_EQ(1, list.datapoints.size());
  EXPECT_EQ("hello", list.datapoints[0].name);
  EXPECT_NE(0, list.datapoints[0].time_ms);
  EXPECT_EQ(0, list.datapoints[0].value);
  EXPECT_EQ(3, list.tags.size());
  EXPECT_EQ("framework_id", list.tags[0].key);
  EXPECT_EQ("fid", list.tags[0].value);
  EXPECT_EQ("executor_id", list.tags[1].key);
  EXPECT_EQ("eid", list.tags[1].value);
  EXPECT_EQ("container_id", list.tags[2].key);
  EXPECT_EQ("cid", list.tags[2].value);
}

TEST_F(AvroEncoderTests, statsd_merge) {
  mesos::ContainerID cid = container_id("cid");

  metrics::container_id_ord_map<metrics_schema::MetricList> map;
  metrics_schema::MetricList& preinit_list = map[cid];
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
  EXPECT_EQ("testk", preinit_list.tags[0].key);
  EXPECT_EQ("valuek", preinit_list.tags[0].value);
  EXPECT_EQ("container_id", preinit_list.tags[1].key);
  EXPECT_EQ("testc", preinit_list.tags[1].value);// original tag left intact
  EXPECT_EQ("framework_id", preinit_list.tags[2].key);
  EXPECT_EQ("fid", preinit_list.tags[2].value);
  EXPECT_EQ("executor_id", preinit_list.tags[3].key);
  EXPECT_EQ("eid", preinit_list.tags[3].value);

  EXPECT_EQ(2, preinit_list.datapoints.size());
  EXPECT_EQ("testn", preinit_list.datapoints[0].name);
  EXPECT_EQ(123, preinit_list.datapoints[0].time_ms);
  EXPECT_EQ(10.3, preinit_list.datapoints[0].value);
  EXPECT_EQ("hello", preinit_list.datapoints[1].name);
  EXPECT_NE(0, preinit_list.datapoints[1].time_ms);
  EXPECT_EQ(3.8, preinit_list.datapoints[1].value);
}

TEST_F(AvroEncoderTests, statsd_parse) {
  //TODO test more of these examples once tag parsing is done

  metrics_schema::MetricList list;

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

  check_no_tags(empty_1tag, 0, "");
  check_no_tags(eempty_1tag, 0, "");
  check_no_tags(hello_noval, 0);
  check_no_tags(hello_noeval, 0);
  check_no_tags(hello_spaceval, 0);
  check_no_tags(hello_wordval, 0);
  check_no_tags(hello_noval_1tag, 0);
  check_no_tags(hello_noval_1tag_noval, 0);
  check_no_tags(hello_spaceval_1tag, 0);
  check_no_tags(hello_wordval_1tag, 0);

  std::string
    hello_noval_2endtag("hello|@0.5|#tag:val"),
    hello_noval_2starttag("hello|#tag:val|@0.5");

  check_no_tags(hello_noval_2endtag, 0);
  check_no_tags(hello_noval_2starttag, 0);

  std::string
    hello_noval_2endtag_noval("hello|@0.5|#tag"),
    hello_noval_2starttag_noval("hello|#ta|@0.5");

  check_no_tags(hello_noval_2endtag_noval, 0);
  check_no_tags(hello_noval_2starttag_noval, 0);

  std::string
    hello_notag("hello:0.35"),
    hello_1tag("hello:0.35|#tag");

  check_no_tags(hello_notag, 0.35);
  check_no_tags(hello_1tag, 0.35);

  std::string
    hello_2endtag("hello:0.35|@0.5|#tag:val"),
    hello_2starttag("hello:0.35|#ta:val|@0.5"),
    hello_2starttag_zerosampling("hello:0.35|#ta:val|@0");

  check_no_tags(hello_2endtag, 0.7);
  check_no_tags(hello_2starttag, 0.7);
  check_no_tags(hello_2starttag_zerosampling, 0.35);

  std::string
    hello_3endtag("hello:0.35|&other|@0.5|#tag:val,t2:v2"),
    hello_3midtag("hello:0.35|&other|#tag:val,t2:v2|@0.5"),
    hello_3midtag_zerosampling("hello:0.35|&other|#tag:val,t2:v2|@0.0"),
    hello_3starttag("hello:0.35|#t:v,t2:v2|&other|@0.5");

  check_no_tags(hello_3endtag, 0.7);
  check_no_tags(hello_3midtag, 0.7);
  check_no_tags(hello_3midtag_zerosampling, 0.35);
  check_no_tags(hello_3starttag, 0.7);

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

  check_no_tags(hello_1emptytag, 0.35);
  check_no_tags(hello_1emptytagval, 0.35);
  check_no_tags(hello_1emptytageval, 0.35);
  check_no_tags(hello_1emptyend, 0.35);

  check_no_tags(hello_2tag_empty, 0.35);
  check_no_tags(hello_2tag_emptyeval, 0.35);
  check_no_tags(hello_2tag_emptyval, 0.35);
  check_no_tags(hello_2empty_empty, 0.35);

  check_no_tags(hello_2empty_tag, 0.35);
  check_no_tags(hello_2empty_tageval, 0.35);
  check_no_tags(hello_2empty_tagval, 0.35);
  check_no_tags(hello_2tag_tag, 0.35);

  check_no_tags(hello_2tag_tageval, 0.35);
  check_no_tags(hello_2tag_tagval, 0.35);
  check_no_tags(hello_2empty_emptytag, 0.35);
  check_no_tags(hello_2empty_emptytagval, 0.35);

  check_no_tags(hello_2empty_emptytageval, 0.35);
  check_no_tags(hello_2emptytag_empty, 0.35);
  check_no_tags(hello_2emptytagval_empty, 0.35);
  check_no_tags(hello_2emptytageval_empty, 0.35);
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

  metrics::container_id_ord_map<metrics_schema::MetricList> metric_map;
  EXPECT_EQ(12, metrics::AvroEncoder::resources_to_struct(usage, metric_map));
  EXPECT_EQ(2, metric_map.size());
  LOG(INFO) << metric_map.begin()->first.value();
  const metrics_schema::MetricList& m1 = metric_map[container_id("cid1-usage")];

  EXPECT_EQ("cid1-usage", m1.topic);

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

  const metrics_schema::MetricList& m2 = metric_map[container_id("cid2-usage")];
  EXPECT_EQ("cid2-usage", m2.topic);

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
