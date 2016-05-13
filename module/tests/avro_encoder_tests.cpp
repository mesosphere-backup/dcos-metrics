#include <fstream>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <avro/Compiler.hh>
#include <avro/DataFile.hh>

#include "avro_encoder.hpp"
#include "metrics_schema_json.hpp"

namespace {
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
    ret.time = time_ms;
    ret.value = val;
    return ret;
  }
  inline metrics_schema::Tag tag(const std::string& key, const std::string& val) {
    metrics_schema::Tag ret;
    ret.key = key;
    ret.value = val;
    return ret;
  }
}

class AvroEncoderTests : public ::testing::Test {
 protected:
  virtual void TearDown() {
    for (const std::string& tmpfile : tmpfiles) {
      LOG(INFO) << "Deleting tmp file: " << tmpfile;
      //unlink(tmpfile.data());
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
  ASSERT_EQ(598, header.size());// just a check for consistency
  EXPECT_EQ('O', header[0]);
  EXPECT_EQ('b', header[1]);
  EXPECT_EQ('j', header[2]);
  EXPECT_EQ('\x01', header[3]);
}

TEST_F(AvroEncoderTests, encode_empty_metrics) {
  metrics::container_id_map<metrics_schema::MetricList> map;
  metrics_schema::MetricList list;
  std::ostringstream oss;
  metrics::AvroEncoder::encode_metrics_block(map, list, oss);
  EXPECT_EQ(0, oss.str().size());

  const std::string header = metrics::AvroEncoder::header();
  const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);

  std::string tmppath = write_tmp(header);

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
    metrics::AvroEncoder::encode_metrics_block(
        metrics::container_id_map<metrics_schema::MetricList>(), list, oss);
    EXPECT_EQ(76, oss.str().size()); // just check for consistency
  }

  std::string tmppath = write_tmp();
  {
    std::ofstream ofs(tmppath, std::ios::binary);
    ofs << metrics::AvroEncoder::header(); // actual file must start with header
    metrics::AvroEncoder::encode_metrics_block(
        metrics::container_id_map<metrics_schema::MetricList>(), list, ofs);
  }
  {
    const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);
    avro::DataFileWriter<metrics_schema::MetricList> writer(write_tmp().data(), schema);
    writer.write(list);
  }

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
    const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);
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

TEST_F(AvroEncoderTests, encode_many_metrics) {
  metrics::container_id_map<metrics_schema::MetricList> map;
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

  list = metric_list("tagged_metrics");
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

  {
    std::ostringstream oss;
    metrics::AvroEncoder::encode_metrics_block(map, list, oss);
    EXPECT_EQ(293, oss.str().size()); // just check for consistency
  }

  std::string tmppath = write_tmp();
  {
    std::ofstream ofs(tmppath, std::ios::binary);
    ofs << metrics::AvroEncoder::header(); // actual file must start with header
    metrics::AvroEncoder::encode_metrics_block(map, list, ofs);
  }
  LOG(INFO) << "A";

  //TODO 02 98 01 between header and start of data (1c 74 61 67...)

  std::ostringstream no_schema_reader_strm, no_schema_data_strm;
  {
    avro::DataFileReader<metrics_schema::MetricList> avro_reader_no_schema(tmppath.data());
    LOG(INFO) << "C";
    EXPECT_FALSE(avro_reader_no_schema.read(list));
    EXPECT_TRUE(list.topic.empty());
    EXPECT_TRUE(list.tags.empty());
    EXPECT_TRUE(list.datapoints.empty());
      LOG(INFO) << "B";

    avro_reader_no_schema.readerSchema().toJson(no_schema_reader_strm);
    avro_reader_no_schema.dataSchema().toJson(no_schema_data_strm);
  }

  std::ostringstream with_schema_reader_strm, with_schema_data_strm;
  {
    const avro::ValidSchema schema = avro::compileJsonSchemaFromString(metrics_schema::SCHEMA_JSON);
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

TEST_F(AvroEncoderTests, map_no_info) {
  EXPECT_TRUE(false) << "TODO expect UNKNOWN_CONTAINER_TAG";
}

TEST_F(AvroEncoderTests, map_with_info) {
  EXPECT_TRUE(false) << "TODO expect correct map key";
}

TEST_F(AvroEncoderTests, statsd_parse) {
  EXPECT_TRUE(false) << "TODO port statsd_tagger_tests";

  std::string hello_1tag("hello|#tag");

  std::string hello_2endtag("hello|@0.5|#tag"), hello_2starttag("hello|#ta|@0.5");

  std::string
    hello_3endtag("hello|&other|@0.5|#tag"),
    hello_3midtag("hello|&other|#tag|@0.5"),
    hello_3starttag("hello|#t|&other|@0.5");

  // corrupt/weird data
  std::string
    hello_1emptytag("hello|#"),
    hello_1emptytagval("hello|#,"),
    hello_1emptyend("hello|"),
    hello_2tag_empty("hello|#tag1|"),
    hello_2empty_empty("hello||"),
    hello_2empty_tag("hello||#tag1"),
    hello_2tag_tag("hello|#tag1|#tag2"),
    hello_2empty_emptytag("hello||#"),
    hello_2emptytag_empty("hello|#|"),
    hello_2emptytagval_empty("hello|#,|");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
