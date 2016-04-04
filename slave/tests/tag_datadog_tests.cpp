#include <glog/logging.h>
#include <gtest/gtest.h>

#include "tag_datadog.cpp" // allow testing of memnmem()

TEST(TagDatadogTests, memnmem) {
  std::string hello("hello"), hey("hey"), hi("hi"), empty("");
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

TEST(TagDatadogTests, prepare_for_tags) {
  std::vector<char> scratch_buffer;
  std::string hello("hello"), hi("hi"), h("h"), empty("");

  EXPECT_EQ(stats::tag_datadog::TagMode::FIRST_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello.data(), hello.size(), scratch_buffer));
  EXPECT_STREQ("hello", hello.data());
  EXPECT_EQ(0, scratch_buffer.size());

  EXPECT_EQ(stats::tag_datadog::TagMode::FIRST_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hi.data(), hi.size(), scratch_buffer));
  EXPECT_STREQ("hi", hi.data());
  EXPECT_EQ(0, scratch_buffer.size());

  EXPECT_EQ(stats::tag_datadog::TagMode::FIRST_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) h.data(), h.size(), scratch_buffer));
  EXPECT_STREQ("h", h.data());
  EXPECT_EQ(0, scratch_buffer.size());

  EXPECT_EQ(stats::tag_datadog::TagMode::FIRST_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) empty.data(), empty.size(), scratch_buffer));
  EXPECT_STREQ("", empty.data());
  EXPECT_EQ(0, scratch_buffer.size());

  std::string hello_1tag("hello|#tag");
  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_1tag.data(), hello_1tag.size(), scratch_buffer));
  EXPECT_STREQ("hello|#tag", hello_1tag.data());
  EXPECT_EQ(0, scratch_buffer.size());// not grown, wasn't used

  std::string hello_2endtag("hello|@0.5|#tag"), hello_2starttag("hello|#ta|@0.5");

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2endtag.data(), hello_2endtag.size(), scratch_buffer));
  EXPECT_STREQ("hello|@0.5|#tag", hello_2endtag.data());
  EXPECT_EQ(0, scratch_buffer.size());// not grown, wasn't used

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2starttag.data(), hello_2starttag.size(), scratch_buffer));
  EXPECT_STREQ("hello|@0.5|#ta", hello_2starttag.data());
  EXPECT_EQ(4, scratch_buffer.size());// grown to fit

  std::string
    hello_3endtag("hello|&other|@0.5|#tag"),
    hello_3midtag("hello|&other|#tag|@0.5"),
    hello_3starttag("hello|#t|&other|@0.5");

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_3endtag.data(), hello_3endtag.size(), scratch_buffer));
  EXPECT_STREQ("hello|&other|@0.5|#tag", hello_3endtag.data());
  EXPECT_EQ(4, scratch_buffer.size());// not grown, wasn't used

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_3midtag.data(), hello_3midtag.size(), scratch_buffer));
  EXPECT_STREQ("hello|&other|@0.5|#tag", hello_3midtag.data());
  EXPECT_EQ(5, scratch_buffer.size());// grown to fit

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_3starttag.data(), hello_3starttag.size(), scratch_buffer));
  EXPECT_STREQ("hello|&other|@0.5|#t", hello_3starttag.data());
  EXPECT_EQ(5, scratch_buffer.size());// only grown, never shrunk

  // corrupt/weird data: doesn't repair, but avoids segfaulting
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

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG_NO_DELIM,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_1emptytagval.data(), hello_1emptytagval.size(), scratch_buffer));
  EXPECT_STREQ("hello|#,", hello_1emptytagval.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG_NO_DELIM,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_1emptytag.data(), hello_1emptytag.size(), scratch_buffer));
  EXPECT_STREQ("hello|#", hello_1emptytag.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::FIRST_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_1emptyend.data(), hello_1emptyend.size(), scratch_buffer));
  EXPECT_STREQ("hello|", hello_1emptyend.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2tag_empty.data(), hello_2tag_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||#tag1", hello_2tag_empty.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::FIRST_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2empty_empty.data(), hello_2empty_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||", hello_2empty_empty.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2empty_tag.data(), hello_2empty_tag.size(), scratch_buffer));
  EXPECT_STREQ("hello||#tag1", hello_2empty_tag.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2tag_tag.data(), hello_2tag_tag.size(), scratch_buffer));
  EXPECT_STREQ("hello|#tag2|#tag1", hello_2tag_tag.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG_NO_DELIM,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2empty_emptytag.data(), hello_2empty_emptytag.size(), scratch_buffer));
  EXPECT_STREQ("hello||#", hello_2empty_emptytag.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG_NO_DELIM,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2emptytag_empty.data(), hello_2emptytag_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||#", hello_2emptytag_empty.data());

  EXPECT_EQ(stats::tag_datadog::TagMode::APPEND_TAG_NO_DELIM,
      stats::tag_datadog::prepare_for_tags(
          (char*) hello_2emptytagval_empty.data(), hello_2emptytagval_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||#,", hello_2emptytagval_empty.data());
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
