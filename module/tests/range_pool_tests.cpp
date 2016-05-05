#include <glog/logging.h>
#include <gtest/gtest.h>

#include "range_pool.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

TEST(RangePoolTests, take_all) {
  metrics::RangePool pool(1, 5);
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

TEST(RangePoolTests, get_all) {
  metrics::RangePool pool(1, 5);
  EXPECT_EQ(2, pool.get(2).get());
  EXPECT_EQ(1, pool.get(1).get());
  EXPECT_TRUE(pool.get(1).isError());
  EXPECT_EQ(4, pool.get(4).get());
  EXPECT_EQ(5, pool.get(5).get());
  EXPECT_EQ(3, pool.get(3).get());
  EXPECT_TRUE(pool.get(1).isError());
  EXPECT_TRUE(pool.get(2).isError());
  EXPECT_TRUE(pool.get(3).isError());
  EXPECT_TRUE(pool.get(4).isError());
  EXPECT_TRUE(pool.get(5).isError());

  pool.put(5);
  EXPECT_EQ(5, pool.get(5).get());
  EXPECT_TRUE(pool.get(5).isError());

  pool.put(4);
  pool.put(3);
  EXPECT_EQ(3, pool.get(3).get());
  EXPECT_EQ(4, pool.get(4).get());
  EXPECT_TRUE(pool.get(4).isError());

  pool.put(1);
  pool.put(3);
  pool.put(5);
  EXPECT_EQ(1, pool.get(1).get());
  pool.put(4);
  EXPECT_EQ(3, pool.get(3).get());
  EXPECT_EQ(5, pool.get(5).get());
  EXPECT_EQ(4, pool.get(4).get());
  EXPECT_TRUE(pool.get(1).isError());
  EXPECT_TRUE(pool.get(2).isError());
  EXPECT_TRUE(pool.get(3).isError());
  EXPECT_TRUE(pool.get(4).isError());
  EXPECT_TRUE(pool.get(5).isError());
}

TEST(RangePoolTests, double_get) {
  metrics::RangePool pool(1, 5);

  EXPECT_EQ(1, pool.get(1).get());
  EXPECT_TRUE(pool.get(1).isError());
  EXPECT_EQ(2, pool.take().get());

  pool.put(1);
  EXPECT_EQ(1, pool.take().get());
  EXPECT_TRUE(pool.get(1).isError());
}

TEST(RangePoolTests, get_exceeds_range) {
  metrics::RangePool pool(2, 5);
  EXPECT_TRUE(pool.get(0).isError());
  EXPECT_TRUE(pool.get(1).isError());
  EXPECT_EQ(2, pool.get(2).get());
  EXPECT_EQ(3, pool.get(3).get());
  EXPECT_EQ(4, pool.get(4).get());
  EXPECT_EQ(5, pool.get(5).get());
  EXPECT_TRUE(pool.get(6).isError());
  EXPECT_TRUE(pool.get(7).isError());
}

TEST(RangePoolTests, double_put) {
  metrics::RangePool pool(1, 5);

  EXPECT_DETH(pool.put(1), ".*1 isn't marked as being used.*");

  EXPECT_EQ(1, pool.take().get());
  pool.put(1);
  EXPECT_DETH(pool.put(1), ".*1 isn't marked as being used.*");

  EXPECT_DETH(pool.put(5), ".*5 isn't marked as being used.*");
}

TEST(RangePoolTests, put_exceeds_range) {
  metrics::RangePool pool(2, 5);
  EXPECT_DETH(pool.put(0), ".*0 is smaller than min value 2.*");
  EXPECT_DETH(pool.put(1), ".*1 is smaller than min value 2.*");
  EXPECT_DETH(pool.put(2), ".*2 isn't marked as being used.*");
  EXPECT_DETH(pool.put(3), ".*3 isn't marked as being used.*");
  EXPECT_DETH(pool.put(4), ".*4 isn't marked as being used.*");
  EXPECT_DETH(pool.put(5), ".*5 isn't marked as being used.*");
  EXPECT_DETH(pool.put(6), ".*6 is larger than max value 5.*");
  EXPECT_DETH(pool.put(7), ".*7 is larger than max value 5.*");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
