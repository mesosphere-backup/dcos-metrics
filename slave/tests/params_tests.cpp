#include <glog/logging.h>
#include <gtest/gtest.h>

#include "params.hpp"

using namespace stats;

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

static void add_param(mesos::Parameters& params, const std::string& key, const std::string& value) {
  mesos::Parameter* p = params.add_parameter();
  p->set_key(key);
  p->set_value(value);
}
static void add_param(mesos::Parameters& params, const std::string& key, int value) {
  add_param(params, key, std::to_string(value));
}

TEST(ParamsTests, to_port_mode) {
  EXPECT_EQ(params::port_mode::UNKNOWN, params::to_port_mode("Single"));
  EXPECT_EQ(params::port_mode::UNKNOWN, params::to_port_mode("single "));
  EXPECT_EQ(params::port_mode::SINGLE, params::to_port_mode("single"));
  EXPECT_EQ(params::port_mode::EPHEMERAL, params::to_port_mode("ephemeral"));
  EXPECT_EQ(params::port_mode::RANGE, params::to_port_mode("range"));
}

TEST(ParamsTests, get_str) {
  mesos::Parameters params;
  EXPECT_EQ("def", params::get_str(params, "k", "def"));
  add_param(params, "k", "v");
  EXPECT_EQ("v", params::get_str(params, "k", "def"));
  add_param(params, "k", "va");
  EXPECT_EQ("v", params::get_str(params, "k", "def"));

  EXPECT_EQ("def", params::get_str(params, "k2", "def"));
  add_param(params, "k2", "v2");
  EXPECT_EQ("v2", params::get_str(params, "k2", "def"));

  params = mesos::Parameters();
  add_param(params, "k3", "");
  EXPECT_DETH(params::get_str(params, "k3", "def"), ".*must be non-empty.*");

}

TEST(ParamsTests, get_uint) {
  mesos::Parameters params;
  EXPECT_EQ(12345, params::get_uint(params, "k", 12345));
  add_param(params, "k", 123);
  EXPECT_EQ(123, params::get_uint(params, "k", 12345));
  add_param(params, "k", 124);
  EXPECT_EQ(123, params::get_uint(params, "k", 12345));

  EXPECT_EQ(12345, params::get_uint(params, "k2", 12345));
  add_param(params, "k2", 321);
  EXPECT_EQ(321, params::get_uint(params, "k2", 12345));

  EXPECT_EQ(12345, params::get_uint(params, "k3", 12345));
  add_param(params, "k3", -1);
  EXPECT_DETH(params::get_uint(params, "k3", 0), ".*must be non-negative.*");

  params = mesos::Parameters();
  add_param(params, "k3", "hi");
  EXPECT_DETH(params::get_uint(params, "k3", 0), ".*must be int.*");
}

TEST(ParamsTests, get_bool) {
  {
    mesos::Parameters params;
    add_param(params, "k", "f");
    EXPECT_FALSE(params::get_bool(params, "k", true));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "false");
    EXPECT_FALSE(params::get_bool(params, "k", true));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "n");
    EXPECT_FALSE(params::get_bool(params, "k", true));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "no");
    EXPECT_FALSE(params::get_bool(params, "k", true));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "0");
    EXPECT_FALSE(params::get_bool(params, "k", true));
  }

  {
    mesos::Parameters params;
    add_param(params, "k", "t");
    EXPECT_TRUE(params::get_bool(params, "k", false));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "true");
    EXPECT_TRUE(params::get_bool(params, "k", false));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "y");
    EXPECT_TRUE(params::get_bool(params, "k", false));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "yes");
    EXPECT_TRUE(params::get_bool(params, "k", false));
  }
  {
    mesos::Parameters params;
    add_param(params, "k", "1");
    EXPECT_TRUE(params::get_bool(params, "k", false));
  }

  mesos::Parameters params;
  EXPECT_FALSE(params::get_bool(params, "k", false));
  add_param(params, "k", "t");
  EXPECT_TRUE(params::get_bool(params, "k", false));
  add_param(params, "k", "f");
  EXPECT_TRUE(params::get_bool(params, "k", false));

  EXPECT_FALSE(params::get_bool(params, "k2", false));
  add_param(params, "k2", "ye");
  EXPECT_TRUE(params::get_bool(params, "k2", false));

  EXPECT_FALSE(params::get_bool(params, "k3", false));
  add_param(params, "k3", "x");
  EXPECT_DETH(params::get_bool(params, "k3", 0), ".*must start with.*");

  params = mesos::Parameters();
  add_param(params, "k3", "hi");
  EXPECT_DETH(params::get_bool(params, "k3", 0), ".*must start with.*");

  params = mesos::Parameters();
  add_param(params, "k3", "2");
  EXPECT_DETH(params::get_bool(params, "k3", 0), ".*must start with.*");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
