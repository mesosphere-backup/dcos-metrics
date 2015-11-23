#include <glog/logging.h>
#include <gtest/gtest.h>

#include "range_pool.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

TEST(RangePoolTests, take_all) {
  stats::RangePool pool(1, 5);
  EXPECT_EQ(1, pool.take().get());
  EXPECT_EQ(2, pool.take().get());
  EXPECT_EQ(3, pool.take().get());
  EXPECT_EQ(4, pool.take().get());
  EXPECT_EQ(5, pool.take().get());
  EXPECT_TRUE(pool.take().isError());

  pool.put(5);
  EXPECT_EQ(5, pool.take().get());
  EXPECT_TRUE(pool.take().isError());

  pool.put(4);
  pool.put(3);
  EXPECT_EQ(3, pool.take().get());
  EXPECT_EQ(4, pool.take().get());
  EXPECT_TRUE(pool.take().isError());

  pool.put(1);
  pool.put(3);
  pool.put(5);
  EXPECT_EQ(1, pool.take().get());
  pool.put(4);
  EXPECT_EQ(3, pool.take().get());
  EXPECT_EQ(4, pool.take().get());
  EXPECT_EQ(5, pool.take().get());
  EXPECT_TRUE(pool.take().isError());
}

TEST(RangePoolTests, double_put) {
  stats::RangePool pool(1, 5);

  EXPECT_DETH(pool.put(1), ".*1 isn't marked as being used.*");

  EXPECT_EQ(1, pool.take().get());
  pool.put(1);
  EXPECT_DETH(pool.put(1), ".*1 isn't marked as being used.*");

  EXPECT_DETH(pool.put(5), ".*5 isn't marked as being used.*");
}

TEST(RangePoolTests, put_exceeds_range) {
  stats::RangePool pool(2, 5);
  EXPECT_DETH(pool.put(0), ".*0 is smaller than min port 2.*");
  EXPECT_DETH(pool.put(1), ".*1 is smaller than min port 2.*");
  EXPECT_DETH(pool.put(2), ".*2 isn't marked as being used.*");

  EXPECT_DETH(pool.put(5), ".*5 isn't marked as being used.*");
  EXPECT_DETH(pool.put(6), ".*6 is larger than max port 5.*");
  EXPECT_DETH(pool.put(7), ".*7 is larger than max port 5.*");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
