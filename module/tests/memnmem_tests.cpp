#include <glog/logging.h>
#include <gtest/gtest.h>

#include "memnmem.h"

namespace {
  const std::string hello("hello"), hey("hey"), hi("hi"), h("h"), empty("");
}

TEST(TaggerTests, memnmem) {
  EXPECT_EQ(NULL, memnmem((char*) empty.data(), empty.size(), empty.data(), empty.size()));
  EXPECT_EQ(NULL, memnmem((char*) hi.data(), hi.size(), empty.data(), empty.size()));
  EXPECT_EQ(NULL, memnmem((char*) empty.data(), empty.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem((char*) hey.data(), hey.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem((char*) hello.data(), hello.size(), hey.data(), hey.size()));
  EXPECT_EQ(NULL, memnmem((char*) hey.data(), hey.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem((char*) hey.data(), hey.size(), hello.data(), hello.size()));
  EXPECT_EQ(NULL, memnmem((char*) hi.data(), hi.size(), hey.data(), hey.size()));
  EXPECT_EQ(hi.data(), memnmem((char*) hi.data(), hi.size(), hi.data(), hi.size()));
  EXPECT_EQ(hey.data(), memnmem((char*) hey.data(), hey.size(), hey.data(), hey.size()));
  EXPECT_EQ(hello.data(),
      memnmem((char*) hello.data(), hello.size(), hello.data(), hello.size()));
  std::string hihey("hihey"), heyhello("heyhello");
  EXPECT_EQ(hihey.data(), memnmem((char*) hihey.data(), hihey.size(), hi.data(), hi.size()));
  EXPECT_STREQ(hey.data(), memnmem((char*) hihey.data(), hihey.size(), hey.data(), hey.size()));
  EXPECT_EQ(heyhello.data(),
      memnmem((char*) heyhello.data(), heyhello.size(), hey.data(), hey.size()));
  EXPECT_STREQ(hello.data(),
      memnmem((char*) heyhello.data(), heyhello.size(), hello.data(), hello.size()));
  std::string h("h"), hhiheyhello("hhiheyhello"), hiheyhello("hiheyhello");
  EXPECT_EQ(hhiheyhello.data(),
      memnmem((char*) hhiheyhello.data(), hhiheyhello.size(), h.data(), h.size()));
  EXPECT_STREQ(hiheyhello.data(),
      memnmem((char*) hhiheyhello.data(), hhiheyhello.size(), hi.data(), hi.size()));
  EXPECT_STREQ(heyhello.data(),
      memnmem((char*) hhiheyhello.data(), hhiheyhello.size(), hey.data(), hey.size()));
  EXPECT_STREQ(hello.data(),
      memnmem((char*) hhiheyhello.data(), hhiheyhello.size(), hello.data(), hello.size()));
  EXPECT_STREQ(heyhello.data(),
      memnmem((char*) hhiheyhello.data(), hhiheyhello.size(), heyhello.data(), heyhello.size()));
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
