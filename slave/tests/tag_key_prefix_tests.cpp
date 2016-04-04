#include <glog/logging.h>
#include <gtest/gtest.h>

#include "tag_key_prefix.cpp" // allow testing of replace_all()

namespace {
  const char FIND = '.', REPLACE = '_';

  void test_replace_all(std::string from, const std::string& expect_to) {
    replace_all((char*)from.data(), from.size(), FIND, REPLACE);
    EXPECT_EQ(expect_to, from);
  }
}

TEST(TagKeyPrefixTests, replace_all) {
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
