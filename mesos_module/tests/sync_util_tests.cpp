#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include "sync_util.hpp"

namespace {
  size_t sleep_a_while(size_t sleep_secs) {
    LOG(INFO) << "RUN: sleep " << sleep_secs;
    sleep(sleep_secs);
    LOG(INFO) << "DONE: sleep " << sleep_secs;
    return sleep_secs;
  }

  size_t return_immediately(size_t return_me) {
    LOG(INFO) << "RUN: ret " << return_me;
    return return_me;
  }

  void run_io_service(std::shared_ptr<boost::asio::io_service> io_service) {
    try {
      LOG(INFO) << "Starting io_service";
      io_service->run();
      LOG(INFO) << "Exited io_service.run()";
    } catch (const std::exception& e) {
      LOG(ERROR) << "io_service.run() threw exception, exiting: " << e.what();
    }
  }

  // create busywork for io_service so that it stays alive during the test
  bool exit = false;
  void loop_until_exit_enabled(std::shared_ptr<boost::asio::io_service> io_service) {
    if (exit) {
      LOG(INFO) << "exit busy loop";
    } else {
      LOG(INFO) << "busy loop";
      boost::asio::deadline_timer timer(*io_service, boost::posix_time::milliseconds(50));
      timer.async_wait(std::bind(&loop_until_exit_enabled, io_service));
    }
  }

  void setup(
      std::shared_ptr<boost::asio::io_service>& io_service,
      std::shared_ptr<std::thread>& thread) {
    io_service.reset(new boost::asio::io_service);

    // schedule busywork
    exit = false;
    loop_until_exit_enabled(io_service);

    thread.reset(new std::thread(std::bind(&run_io_service, io_service)));
    LOG(INFO) << "setup done";
  }

  void shutdown(
      std::shared_ptr<boost::asio::io_service>& io_service,
      std::shared_ptr<std::thread>& thread) {
    LOG(INFO) << "start shutdown";

    // tell busywork to stop
    exit = true;

    io_service->stop();
    thread->join();
    thread.reset();
    io_service->reset();
    io_service.reset();
  }
}

TEST(SyncUtilTests, fast_func_get) {
  std::shared_ptr<boost::asio::io_service> io_service;
  std::shared_ptr<std::thread> io_service_thread;
  setup(io_service, io_service_thread);

  size_t val = 1234;
  std::function<size_t()> fast_func = std::bind(&return_immediately, val);
  std::shared_ptr<size_t> out = metrics::sync_util::dispatch_get(
      "return_immediately_get", *io_service, fast_func, 1 /* timeout */);

  ASSERT_TRUE((bool) out);
  EXPECT_EQ(val, *out);

  shutdown(io_service, io_service_thread);
}

TEST(SyncUtilTests, fast_func_run) {
  std::shared_ptr<boost::asio::io_service> io_service;
  std::shared_ptr<std::thread> io_service_thread;
  setup(io_service, io_service_thread);

  size_t val = 1234;
  std::function<size_t()> fast_func = std::bind(&return_immediately, val);

  // break out response into separate variable: weird macro issues with running this direct
  bool ret = metrics::sync_util::dispatch_run(
      "return_immediately_run", *io_service, fast_func, 1 /* timeout */);
  EXPECT_TRUE(ret);

  shutdown(io_service, io_service_thread);
}

TEST(SyncUtilTests, slow_func_get) {
  std::shared_ptr<boost::asio::io_service> io_service;
  std::shared_ptr<std::thread> io_service_thread;
  setup(io_service, io_service_thread);

  size_t timeout = 1;
  // make the func run more slowly than the timeout:
  std::function<size_t()> slow_func = std::bind(&sleep_a_while, timeout + 1);

  std::shared_ptr<size_t> out = metrics::sync_util::dispatch_get(
      "sleep_a_while_get", *io_service, slow_func, timeout);

  EXPECT_FALSE((bool) out);

  shutdown(io_service, io_service_thread);
}

TEST(SyncUtilTests, slow_func_run) {
  std::shared_ptr<boost::asio::io_service> io_service;
  std::shared_ptr<std::thread> io_service_thread;
  setup(io_service, io_service_thread);

  size_t timeout = 1;
  // make the func run more slowly than the timeout:
  std::function<size_t()> slow_func = std::bind(&sleep_a_while, timeout + 1);

  // break out response into separate variable: weird macro issues with running this direct
  bool ret = metrics::sync_util::dispatch_run(
      "sleep_a_while_run", *io_service, slow_func, timeout);
  EXPECT_FALSE(ret);

  shutdown(io_service, io_service_thread);
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
