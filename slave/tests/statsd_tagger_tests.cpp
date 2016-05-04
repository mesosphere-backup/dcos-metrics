#include <glog/logging.h>
#include <gtest/gtest.h>

#include "statsd_tagger.cpp" // allow testing of replace_all() and memnmem()

namespace {
  const char FIND = '.', REPLACE = '_';

  void test_replace_all(std::string from, const std::string& expect_to) {
    replace_all((char*)from.data(), from.size(), FIND, REPLACE);
    EXPECT_EQ(expect_to, from);
  }

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

  // exercise . -> _ conversion:
  mesos::ContainerID cid = container_id("c.id");
  mesos::ExecutorInfo ei = exec_info("f.id", "e.id");

  const std::string hello("hello"), hey("hey"), hi("hi"), h("h"), empty("");
}

TEST(TaggerTests, null_tagger_no_container) {
  stats::NullTagger tagger;
  EXPECT_EQ(hello.size(), tagger.calculate_size(NULL, NULL, hello.data(), hello.size()));
  EXPECT_EQ(hey.size(), tagger.calculate_size(NULL, NULL, hey.data(), hey.size()));
  EXPECT_EQ(hi.size(), tagger.calculate_size(NULL, NULL, hi.data(), hi.size()));
  EXPECT_EQ(h.size(), tagger.calculate_size(NULL, NULL, h.data(), h.size()));
  EXPECT_EQ(empty.size(), tagger.calculate_size(NULL, NULL, empty.data(), empty.size()));

  std::vector<char> buf(100,'\0');

  tagger.tag_copy(NULL, NULL, hello.data(), hello.size(), buf.data());
  std::string got(buf.data(), hello.size());
  EXPECT_EQ(hello, got);

  tagger.tag_copy(NULL, NULL, hey.data(), hey.size(), buf.data());
  got = std::string(buf.data(), hey.size());
  EXPECT_EQ(hey, got);

  tagger.tag_copy(NULL, NULL, hi.data(), hi.size(), buf.data());
  got = std::string(buf.data(), hi.size());
  EXPECT_EQ(hi, got);

  tagger.tag_copy(NULL, NULL, h.data(), h.size(), buf.data());
  got = std::string(buf.data(), h.size());
  EXPECT_EQ(h, got);

  tagger.tag_copy(NULL, NULL, empty.data(), empty.size(), buf.data());
  got = std::string(buf.data(), empty.size());
  EXPECT_EQ(empty, got);
}

TEST(TaggerTests, null_tagger_with_container) {
  stats::NullTagger tagger;
  EXPECT_EQ(hello.size(), tagger.calculate_size(&cid, &ei, hello.data(), hello.size()));
  EXPECT_EQ(hey.size(), tagger.calculate_size(&cid, &ei, hey.data(), hey.size()));
  EXPECT_EQ(hi.size(), tagger.calculate_size(&cid, &ei, hi.data(), hi.size()));
  EXPECT_EQ(h.size(), tagger.calculate_size(&cid, &ei, h.data(), h.size()));
  EXPECT_EQ(empty.size(), tagger.calculate_size(&cid, &ei, empty.data(), empty.size()));

  std::vector<char> buf(100,'\0');

  tagger.tag_copy(&cid, &ei, hello.data(), hello.size(), buf.data());
  std::string got(buf.data(), hello.size());
  EXPECT_EQ(hello, got);

  tagger.tag_copy(&cid, &ei, hey.data(), hey.size(), buf.data());
  got = std::string(buf.data(), hey.size());
  EXPECT_EQ(hey, got);

  tagger.tag_copy(&cid, &ei, hi.data(), hi.size(), buf.data());
  got = std::string(buf.data(), hi.size());
  EXPECT_EQ(hi, got);

  tagger.tag_copy(&cid, &ei, h.data(), h.size(), buf.data());
  got = std::string(buf.data(), h.size());
  EXPECT_EQ(h, got);

  tagger.tag_copy(&cid, &ei, empty.data(), empty.size(), buf.data());
  got = std::string(buf.data(), empty.size());
  EXPECT_EQ(empty, got);
}

//---

TEST(TaggerTests, key_prefix_tagger_no_container) {
  stats::KeyPrefixTagger tagger;
  std::string prefix = UNKNOWN_CONTAINER_TAG + ".";

  EXPECT_EQ(prefix.size() + hello.size(),
      tagger.calculate_size(NULL, NULL, hello.data(), hello.size()));
  EXPECT_EQ(prefix.size() + hey.size(),
      tagger.calculate_size(NULL, NULL, hey.data(), hey.size()));
  EXPECT_EQ(prefix.size() + hi.size(),
      tagger.calculate_size(NULL, NULL, hi.data(), hi.size()));
  EXPECT_EQ(prefix.size() + h.size(),
      tagger.calculate_size(NULL, NULL, h.data(), h.size()));
  EXPECT_EQ(prefix.size() + empty.size(),
      tagger.calculate_size(NULL, NULL, empty.data(), empty.size()));

  std::vector<char> buf(100,'\0');

  std::string expect = prefix + hello;
  tagger.tag_copy(NULL, NULL, hello.data(), hello.size(), buf.data());
  std::string got(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + hey;
  tagger.tag_copy(NULL, NULL, hey.data(), hey.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + hi;
  tagger.tag_copy(NULL, NULL, hi.data(), hi.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + h;
  tagger.tag_copy(NULL, NULL, h.data(), h.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + empty;
  tagger.tag_copy(NULL, NULL, empty.data(), empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);
}

TEST(TaggerTests, key_prefix_tagger_with_container) {
  stats::KeyPrefixTagger tagger;
  std::string prefix = "f_id.e_id.c_id.";

  EXPECT_EQ(prefix.size() + hello.size(),
      tagger.calculate_size(&cid, &ei, hello.data(), hello.size()));
  EXPECT_EQ(prefix.size() + hey.size(),
      tagger.calculate_size(&cid, &ei, hey.data(), hey.size()));
  EXPECT_EQ(prefix.size() + hi.size(),
      tagger.calculate_size(&cid, &ei, hi.data(), hi.size()));
  EXPECT_EQ(prefix.size() + h.size(),
      tagger.calculate_size(&cid, &ei, h.data(), h.size()));
  EXPECT_EQ(prefix.size() + empty.size(),
      tagger.calculate_size(&cid, &ei, empty.data(), empty.size()));

  std::vector<char> buf(100,'\0');

  std::string expect = prefix + hello;
  tagger.tag_copy(&cid, &ei, hello.data(), hello.size(), buf.data());
  std::string got(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + hey;
  tagger.tag_copy(&cid, &ei, hey.data(), hey.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + hi;
  tagger.tag_copy(&cid, &ei, hi.data(), hi.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + h;
  tagger.tag_copy(&cid, &ei, h.data(), h.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = prefix + empty;
  tagger.tag_copy(&cid, &ei, empty.data(), empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);
}

// ---

TEST(TaggerTests, datadog_tagger_no_container) {
  stats::DatadogTagger tagger;
  std::string suffix = "|#" + UNKNOWN_CONTAINER_TAG;
  std::vector<char> buf(100,'\0');

  EXPECT_EQ(hello.size() + suffix.size(),
      tagger.calculate_size(NULL, NULL, hello.data(), hello.size()));
  std::string expect = hello + suffix;
  tagger.tag_copy(NULL, NULL, hello.data(), hello.size(), buf.data());
  std::string got(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(hey.size() + suffix.size(),
      tagger.calculate_size(NULL, NULL, hey.data(), hey.size()));
  expect = hey + suffix;
  tagger.tag_copy(NULL, NULL, hey.data(), hey.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(hi.size() + suffix.size(),
      tagger.calculate_size(NULL, NULL, hi.data(), hi.size()));
  expect = hi + suffix;
  tagger.tag_copy(NULL, NULL, hi.data(), hi.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(h.size() + suffix.size(),
      tagger.calculate_size(NULL, NULL, h.data(), h.size()));
  expect = h + suffix;
  tagger.tag_copy(NULL, NULL, h.data(), h.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(empty.size() + suffix.size(),
      tagger.calculate_size(NULL, NULL, empty.data(), empty.size()));
  expect = empty + suffix;
  tagger.tag_copy(NULL, NULL, empty.data(), empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);
}

TEST(TaggerTests, datadog_tagger_with_container) {
  stats::DatadogTagger tagger;
  std::string suffix = "|#" + FRAMEWORK_ID_DATADOG_KEY + ":f.id,"
    + EXECUTOR_ID_DATADOG_KEY + ":e.id,"
    + CONTAINER_ID_DATADOG_KEY + ":c.id";
  std::vector<char> buf(100,'\0');

  EXPECT_EQ(hello.size() + suffix.size(),
      tagger.calculate_size(&cid, &ei, hello.data(), hello.size()));
  std::string expect = hello + suffix;
  tagger.tag_copy(&cid, &ei, hello.data(), hello.size(), buf.data());
  std::string got(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(hey.size() + suffix.size(),
      tagger.calculate_size(&cid, &ei, hey.data(), hey.size()));
  expect = hey + suffix;
  tagger.tag_copy(&cid, &ei, hey.data(), hey.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(hi.size() + suffix.size(),
      tagger.calculate_size(&cid, &ei, hi.data(), hi.size()));
  expect = hi + suffix;
  tagger.tag_copy(&cid, &ei, hi.data(), hi.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(h.size() + suffix.size(),
      tagger.calculate_size(&cid, &ei, h.data(), h.size()));
  expect = h + suffix;
  tagger.tag_copy(&cid, &ei, h.data(), h.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  EXPECT_EQ(empty.size() + suffix.size(),
      tagger.calculate_size(&cid, &ei, empty.data(), empty.size()));
  expect = empty + suffix;
  tagger.tag_copy(&cid, &ei, empty.data(), empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);
}

TEST(TaggerTests, datadog_corner_cases_no_container) {
  stats::DatadogTagger tagger;
  std::vector<char> buf(100,'\0');

  std::string hello_1tag("hello|#tag");

  std::string expect = hello_1tag + "," + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_1tag.data(), hello_1tag.size()));
  tagger.tag_copy(NULL, NULL, hello_1tag.data(), hello_1tag.size(), buf.data());
  std::string got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  std::string hello_2endtag("hello|@0.5|#tag"), hello_2starttag("hello|#ta|@0.5");

  expect = hello_2endtag + "," + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2endtag.data(), hello_2endtag.size()));
  tagger.tag_copy(NULL, NULL, hello_2endtag.data(), hello_2endtag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#ta," + UNKNOWN_CONTAINER_TAG + "|@0.5";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2starttag.data(), hello_2starttag.size()));
  tagger.tag_copy(NULL, NULL, hello_2starttag.data(), hello_2starttag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  std::string
    hello_3endtag("hello|&other|@0.5|#tag"),
    hello_3midtag("hello|&other|#tag|@0.5"),
    hello_3starttag("hello|#t|&other|@0.5");

  expect = hello_3endtag + "," + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_3endtag.data(), hello_3endtag.size()));
  tagger.tag_copy(NULL, NULL, hello_3endtag.data(), hello_3endtag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|&other|#tag," + UNKNOWN_CONTAINER_TAG + "|@0.5";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_3midtag.data(), hello_3midtag.size()));
  tagger.tag_copy(NULL, NULL, hello_3midtag.data(), hello_3midtag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#t," + UNKNOWN_CONTAINER_TAG + "|&other|@0.5";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_3starttag.data(), hello_3starttag.size()));
  tagger.tag_copy(NULL, NULL, hello_3starttag.data(), hello_3starttag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

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

  expect = hello_1emptytag + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_1emptytag.data(), hello_1emptytag.size()));
  tagger.tag_copy(NULL, NULL, hello_1emptytag.data(), hello_1emptytag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = hello_1emptytagval + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_1emptytagval.data(), hello_1emptytagval.size()));
  tagger.tag_copy(NULL, NULL, hello_1emptytagval.data(), hello_1emptytagval.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = hello_1emptyend + "|#" + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_1emptyend.data(), hello_1emptyend.size()));
  tagger.tag_copy(NULL, NULL, hello_1emptyend.data(), hello_1emptyend.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#tag1," + UNKNOWN_CONTAINER_TAG + "|";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2tag_empty.data(), hello_2tag_empty.size()));
  tagger.tag_copy(NULL, NULL, hello_2tag_empty.data(), hello_2tag_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|||#" + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2empty_empty.data(), hello_2empty_empty.size()));
  tagger.tag_copy(NULL, NULL, hello_2empty_empty.data(), hello_2empty_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello||#tag1," + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2empty_tag.data(), hello_2empty_tag.size()));
  tagger.tag_copy(NULL, NULL, hello_2empty_tag.data(), hello_2empty_tag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#tag1," + UNKNOWN_CONTAINER_TAG + "|#tag2";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2tag_tag.data(), hello_2tag_tag.size()));
  tagger.tag_copy(NULL, NULL, hello_2tag_tag.data(), hello_2tag_tag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello||#" + UNKNOWN_CONTAINER_TAG;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2empty_emptytag.data(), hello_2empty_emptytag.size()));
  tagger.tag_copy(NULL, NULL, hello_2empty_emptytag.data(), hello_2empty_emptytag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#" + UNKNOWN_CONTAINER_TAG + "|";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2emptytag_empty.data(), hello_2emptytag_empty.size()));
  tagger.tag_copy(NULL, NULL, hello_2emptytag_empty.data(), hello_2emptytag_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#," + UNKNOWN_CONTAINER_TAG + "|";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(NULL, NULL, hello_2emptytagval_empty.data(), hello_2emptytagval_empty.size()));
  tagger.tag_copy(NULL, NULL, hello_2emptytagval_empty.data(), hello_2emptytagval_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);
}

TEST(TaggerTests, datadog_corner_cases_with_container) {
  stats::DatadogTagger tagger;
  std::string tags = FRAMEWORK_ID_DATADOG_KEY + ":f.id,"
    + EXECUTOR_ID_DATADOG_KEY + ":e.id,"
    + CONTAINER_ID_DATADOG_KEY + ":c.id";
  std::vector<char> buf(100,'\0');

  std::string hello_1tag("hello|#tag");

  std::string expect = hello_1tag + "," + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_1tag.data(), hello_1tag.size()));
  tagger.tag_copy(&cid, &ei, hello_1tag.data(), hello_1tag.size(), buf.data());
  std::string got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  std::string hello_2endtag("hello|@0.5|#tag"), hello_2starttag("hello|#ta|@0.5");

  expect = hello_2endtag + "," + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2endtag.data(), hello_2endtag.size()));
  tagger.tag_copy(&cid, &ei, hello_2endtag.data(), hello_2endtag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#ta," + tags + "|@0.5";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2starttag.data(), hello_2starttag.size()));
  tagger.tag_copy(&cid, &ei, hello_2starttag.data(), hello_2starttag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  std::string
    hello_3endtag("hello|&other|@0.5|#tag"),
    hello_3midtag("hello|&other|#tag|@0.5"),
    hello_3starttag("hello|#t|&other|@0.5");

  expect = hello_3endtag + "," + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_3endtag.data(), hello_3endtag.size()));
  tagger.tag_copy(&cid, &ei, hello_3endtag.data(), hello_3endtag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|&other|#tag," + tags + "|@0.5";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_3midtag.data(), hello_3midtag.size()));
  tagger.tag_copy(&cid, &ei, hello_3midtag.data(), hello_3midtag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#t," + tags + "|&other|@0.5";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_3starttag.data(), hello_3starttag.size()));
  tagger.tag_copy(&cid, &ei, hello_3starttag.data(), hello_3starttag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

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

  expect = hello_1emptytag + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_1emptytag.data(), hello_1emptytag.size()));
  tagger.tag_copy(&cid, &ei, hello_1emptytag.data(), hello_1emptytag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = hello_1emptytagval + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_1emptytagval.data(), hello_1emptytagval.size()));
  tagger.tag_copy(&cid, &ei, hello_1emptytagval.data(), hello_1emptytagval.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = hello_1emptyend + "|#" + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_1emptyend.data(), hello_1emptyend.size()));
  tagger.tag_copy(&cid, &ei, hello_1emptyend.data(), hello_1emptyend.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#tag1," + tags + "|";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2tag_empty.data(), hello_2tag_empty.size()));
  tagger.tag_copy(&cid, &ei, hello_2tag_empty.data(), hello_2tag_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|||#" + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2empty_empty.data(), hello_2empty_empty.size()));
  tagger.tag_copy(&cid, &ei, hello_2empty_empty.data(), hello_2empty_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello||#tag1," + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2empty_tag.data(), hello_2empty_tag.size()));
  tagger.tag_copy(&cid, &ei, hello_2empty_tag.data(), hello_2empty_tag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#tag1," + tags + "|#tag2";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2tag_tag.data(), hello_2tag_tag.size()));
  tagger.tag_copy(&cid, &ei, hello_2tag_tag.data(), hello_2tag_tag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello||#" + tags;
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2empty_emptytag.data(), hello_2empty_emptytag.size()));
  tagger.tag_copy(&cid, &ei, hello_2empty_emptytag.data(), hello_2empty_emptytag.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#" + tags + "|";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2emptytag_empty.data(), hello_2emptytag_empty.size()));
  tagger.tag_copy(&cid, &ei, hello_2emptytag_empty.data(), hello_2emptytag_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);

  expect = "hello|#," + tags + "|";
  EXPECT_EQ(expect.size(),
      tagger.calculate_size(&cid, &ei, hello_2emptytagval_empty.data(), hello_2emptytagval_empty.size()));
  tagger.tag_copy(&cid, &ei, hello_2emptytagval_empty.data(), hello_2emptytagval_empty.size(), buf.data());
  got = std::string(buf.data(), expect.size());
  EXPECT_EQ(expect, got);
}

//---

TEST(TaggerTests, memnmem) {
  EXPECT_EQ(NULL, memnmem_imp((char*) empty.data(), empty.size(), empty.data(), empty.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hi.data(), hi.size(), empty.data(), empty.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) empty.data(), empty.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hey.data(), hey.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hello.data(), hello.size(), hey.data(), hey.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hey.data(), hey.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hey.data(), hey.size(), hello.data(), hello.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hi.data(), hi.size(), hey.data(), hey.size()));
  EXPECT_EQ(hi.data(), memnmem_imp((char*) hi.data(), hi.size(), hi.data(), hi.size()));
  EXPECT_EQ(hey.data(), memnmem_imp((char*) hey.data(), hey.size(), hey.data(), hey.size()));
  EXPECT_EQ(hello.data(),
      memnmem_imp((char*) hello.data(), hello.size(), hello.data(), hello.size()));
  std::string hihey("hihey"), heyhello("heyhello");
  EXPECT_EQ(hihey.data(), memnmem_imp((char*) hihey.data(), hihey.size(), hi.data(), hi.size()));
  EXPECT_STREQ(hey.data(), memnmem_imp((char*) hihey.data(), hihey.size(), hey.data(), hey.size()));
  EXPECT_EQ(heyhello.data(),
      memnmem_imp((char*) heyhello.data(), heyhello.size(), hey.data(), hey.size()));
  EXPECT_STREQ(hello.data(),
      memnmem_imp((char*) heyhello.data(), heyhello.size(), hello.data(), hello.size()));
  std::string h("h"), hhiheyhello("hhiheyhello"), hiheyhello("hiheyhello");
  EXPECT_EQ(hhiheyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), h.data(), h.size()));
  EXPECT_STREQ(hiheyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), hi.data(), hi.size()));
  EXPECT_STREQ(heyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), hey.data(), hey.size()));
  EXPECT_STREQ(hello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), hello.data(), hello.size()));
  EXPECT_STREQ(heyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), heyhello.data(), heyhello.size()));
}

TEST(TaggerTests, replace_all) {
  test_replace_all("", "");
  test_replace_all(".", "_");
  test_replace_all("a", "a");
  test_replace_all(".a", "_a");
  test_replace_all("a.", "a_");
  test_replace_all("a.a", "a_a");
  test_replace_all(".a.", "_a_");
  test_replace_all("a.a.a", "a_a_a");
  test_replace_all("hello there.", "hello there_");
  test_replace_all(".sotehuson.noseth.oideson.", "_sotehuson_noseth_oideson_");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
