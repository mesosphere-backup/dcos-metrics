#include <glog/logging.h>
#include <gtest/gtest.h>
#include <stout/os.hpp>

#include <thread>

#include "input_state_cache_impl.hpp"

class InputStateCacheTests : public ::testing::Test {
 protected:
  virtual void SetUp() {
    //mkdir /tmp/test-<pid>
    std::string template_copy = "input_state_cache_impl_tests-XXXXXX";
    if (mkdtemp((char*)template_copy.c_str()) == NULL) {
      LOG(FATAL) << "Failed to create tmpdir";
    }
    root_path_ = template_copy + "/";
    // use a nested path within the created directory: ensure that code handles nested mkdir
    config_path_ = root_path_ + "test/config/path/";
    container_path_ = config_path_ + "containers/";
    LOG(INFO) << "Using root_path[" << root_path_ << "] "
              << "config_path[" << config_path_ << "] "
              << "container_path [" << container_path_ << "]";
  }

  virtual void TearDown() {
    LOG(INFO) << "Deleting everything in root_path[" << root_path_ << "]";
    Try<Nothing> result = os::rmdir(root_path_);
    if (result.isError()) {
      LOG(FATAL) << "Failed to clean up root_path[" << root_path_ << "]: " << result.error();
    }
  }

  mesos::Parameters get_path_params() const {
    mesos::Parameters params;
    mesos::Parameter* param = params.add_parameter();
    param->set_key(stats::params::STATE_PATH_DIR);
    param->set_value(config_path());
    return params;
  }

  mesos::ContainerID container_id(const std::string& str) const {
    mesos::ContainerID id;
    id.set_value(str);
    return id;
  }

  const std::string& root_path() const {
    return root_path_;
  }
  const std::string& config_path() const {
    return config_path_;
  }
  const std::string& container_path() const {
    return container_path_;
  }

 private:
  // both with trailing slash:
  std::string root_path_, config_path_, container_path_;
};

TEST_F(InputStateCacheTests, init_does_very_little) {
  stats::InputStateCacheImpl cache(get_path_params());
  EXPECT_EQ(config_path(), cache.path());
  EXPECT_TRUE(os::exists(root_path()));
  EXPECT_FALSE(os::exists(cache.path()));
  EXPECT_TRUE(cache.get_containers().empty());
}

TEST_F(InputStateCacheTests, single_get_add_get_remove_get) {
  mesos::ContainerID id = container_id("hello");
  stats::UDPEndpoint endpoint("host-hello", 123);

  // use scoping to sorta validate that state is preserved across instances:

  {
    stats::InputStateCacheImpl cache(get_path_params());
    EXPECT_TRUE(cache.get_containers().empty());

    cache.add_container(id, endpoint);
    EXPECT_TRUE(os::exists(container_path() + id.value()));
  }
  {
    stats::InputStateCacheImpl cache(get_path_params());
    stats::container_id_map<stats::UDPEndpoint> map = cache.get_containers();

    EXPECT_EQ(1, map.size());
    EXPECT_EQ(endpoint, map.find(id)->second);

    EXPECT_TRUE(os::exists(container_path() + id.value()));
    cache.remove_container(id);
    EXPECT_FALSE(os::exists(container_path() + id.value()));
  }
  {
    stats::InputStateCacheImpl cache(get_path_params());
    EXPECT_TRUE(cache.get_containers().empty());
  }
}

TEST_F(InputStateCacheTests, multi_get_add_get_remove_get) {
  mesos::ContainerID id1 = container_id("hello"), id2 = container_id("hi");
  stats::UDPEndpoint endpoint1("host-hello", 123), endpoint2("host-hi", 234);

  stats::InputStateCacheImpl cache(get_path_params());
  EXPECT_TRUE(cache.get_containers().empty());

  EXPECT_FALSE(os::exists(container_path() + id1.value()));
  cache.add_container(id1, endpoint1);
  EXPECT_TRUE(os::exists(container_path() + id1.value()));

  stats::container_id_map<stats::UDPEndpoint> map = cache.get_containers();
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(endpoint1, map.find(id1)->second);

  EXPECT_FALSE(os::exists(container_path() + id2.value()));
  cache.add_container(id2, endpoint2);
  EXPECT_TRUE(os::exists(container_path() + id2.value()));

  map = cache.get_containers();
  EXPECT_EQ(2, map.size());
  EXPECT_EQ(endpoint1, map.find(id1)->second);
  EXPECT_EQ(endpoint2, map.find(id2)->second);

  cache.remove_container(id2);

  map = cache.get_containers();
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(endpoint1, map.find(id1)->second);

  cache.remove_container(id1);
  EXPECT_TRUE(cache.get_containers().empty());
}

TEST_F(InputStateCacheTests, malicious_container_id) {
  mesos::ContainerID bad_id = container_id("../../../etc/shadow");
  stats::UDPEndpoint endpoint("host-bad", 123);

  stats::InputStateCacheImpl cache(get_path_params());

  // expect bad id to result in sanitized path:
  cache.add_container(bad_id, endpoint);
  EXPECT_TRUE(os::exists(container_path() + "......etcshadow"));

  // original verbatim path is returned in container list:
  stats::container_id_map<stats::UDPEndpoint> map = cache.get_containers();
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(endpoint, map.find(bad_id)->second);

  // removal of bad id finds sanitized path:
  cache.remove_container(bad_id);
  EXPECT_FALSE(os::exists(container_path() + "......etcshadow"));
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
