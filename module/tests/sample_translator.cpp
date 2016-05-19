
#include <stout/os.hpp>
#include <stout/path.hpp>

#include "container_assigner.hpp"
#include "io_runner.hpp"
#include "module_access_factory.hpp"

#define STUB_FRAMEWORK_ID "stub-framework-id"
#define STUB_EXECUTOR_ID "stub-executor-id"
#define STUB_CONTAINER_ID "stub-container-id"
#define USAGE_REFRESH_PERIOD_SECS 15
#define TMPDIR_TEMPLATE_PREFIX "sample_translator-tmp-"

/**
 * Runs the statsd->TCP/UDP logic, looping on an input file. Used to exercise/verify the translation logic
 * without requiring installation onto a mesos agent.
 */
int main(int argc, char* argv[]) {
  if (argc != 4) {
    fprintf(stderr,
        "Usage: %s <input_statsd_port> <output_collector_port> <output_statsd_port>\n", argv[0]);
    return -1;
  }

  // clean existing tmpdirs
  const std::string cwd = os::getcwd();
  Try<std::list<std::string>> filenames = os::ls(cwd);
  if (filenames.isError()) {
    LOG(ERROR) << "Failed to list contents of '" << cwd << "': " << filenames.error();
  } else {
    for (const std::string& filename : filenames.get()) {
      if (strings::startsWith(filename, TMPDIR_TEMPLATE_PREFIX)
          && os::stat::isdir(filename)) {
        LOG(INFO) << "Deleting preexisting stale tmpdir '" << filename << "'";
        Try<Nothing> result = os::rmdir(filename);
        if (result.isError()) {
          LOG(ERROR) << "Failed to delete stale tmpdir '" << filename << "': " << result.error();
        }
      }
    }
  }


  Try<std::string> tmpdir =
    os::mkdtemp(path::join(cwd, TMPDIR_TEMPLATE_PREFIX "XXXXXX"));
  if (tmpdir.isError()) {
    LOG(FATAL) << "Unable to create temp dir within " << cwd;
    return -1;
  }

  mesos::Parameters params;
  {
    mesos::Parameter* param = params.add_parameter();
    param->set_key(metrics::params::LISTEN_PORT_MODE);
    param->set_value(metrics::params::LISTEN_PORT_MODE_SINGLE);

    param = params.add_parameter();
    param->set_key(metrics::params::LISTEN_PORT);
    param->set_value(argv[1]);
    LOG(INFO) << "STATSD IN:  " << param->value();

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_COLLECTOR_PORT);
    param->set_value(argv[2]);
    LOG(INFO) << "AVRO OUT:   " << param->value();

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_HOST);
    param->set_value("127.0.0.1");

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_PORT);
    param->set_value(argv[3]);
    LOG(INFO) << "STATSD OUT: " << param->value();

    param = params.add_parameter();
    param->set_key(metrics::params::STATE_PATH_DIR);
    param->set_value(tmpdir.get());
    LOG(INFO) << "TEMP DIR:   " << tmpdir.get() << "\n";
  }

  // prepare stub ids
  mesos::ContainerID container_id;
  container_id.set_value(STUB_CONTAINER_ID);
  mesos::ExecutorInfo executor_info;
  executor_info.mutable_framework_id()->set_value(STUB_FRAMEWORK_ID);
  executor_info.mutable_executor_id()->set_value(STUB_EXECUTOR_ID);

  // start up the processing thread for accepting statsd data on the given port
  std::shared_ptr<metrics::ContainerAssigner> container_assigner =
    metrics::ModuleAccessFactory::get_container_assigner(params);
  std::shared_ptr<metrics::IORunner> io_runner =
    metrics::ModuleAccessFactory::get_io_runner(params);

  // launch the port reader
  for (;;) {
    Try<metrics::UDPEndpoint> endpoint =
      container_assigner->register_container(container_id, executor_info);
    if (!endpoint.isError()) {
      break;
    }
    LOG(ERROR) << "Unable to register endpoint, trying again: " << endpoint.error();
  }

  // on this main thread, loop forever and emit fake usage stats
  mesos::ResourceUsage sample_usage;
  mesos::ResourceUsage_Executor* executor = sample_usage.add_executors();
  *executor->mutable_container_id() = container_id;
  *executor->mutable_executor_info() = executor_info;

  mesos::ResourceStatistics* statistics = executor->mutable_statistics();
  // timestamp filled below
  statistics->set_cpus_user_time_secs(0.03);
  statistics->set_cpus_system_time_secs(0.01);
  statistics->set_cpus_limit(1.1);
  statistics->set_mem_rss_bytes(2584576);
  statistics->set_mem_limit_bytes(167772160);
  statistics->set_cpus_nr_periods(916);
  statistics->set_cpus_nr_throttled(0);
  statistics->set_cpus_throttled_time_secs(0.009895515);
  statistics->set_mem_file_bytes(0);
  statistics->set_mem_anon_bytes(2584576);
  statistics->set_mem_mapped_file_bytes(0);
  statistics->set_mem_low_pressure_counter(0);
  statistics->set_mem_medium_pressure_counter(0);
  statistics->set_mem_critical_pressure_counter(0);
  statistics->set_mem_total_bytes(2584576);
  statistics->set_mem_cache_bytes(0);
  statistics->set_mem_swap_bytes(0);
  statistics->set_mem_unevictable_bytes(0);
  for (;;) {
    statistics->set_timestamp(time(NULL));
    io_runner->update_usage(process::Future<mesos::ResourceUsage>(sample_usage));
    sleep(USAGE_REFRESH_PERIOD_SECS);
  }
}
