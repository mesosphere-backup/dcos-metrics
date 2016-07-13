#include <stout/os.hpp>
#include <stout/path.hpp>

#include "container_assigner.hpp"
#include "io_runner.hpp"
#include "module_access_factory.hpp"

#define STUB_FRAMEWORK_ID "stub-framework-id"
#define STUB_EXECUTOR_ID "stub-executor-id"
#define STUB_CONTAINER_ID "stub-container-id"
#define SLEEP_PERIOD_SECS 15
#define TMPDIR_TEMPLATE_PREFIX "sample_translator-tmp-"

/**
 * Runs the statsd->TCP/UDP logic, looping on an input file. Used to exercise/verify the translation logic
 * without requiring installation onto a mesos agent.
 */
int main(int argc, char* argv[]) {
  if (argc != 4) {
    fprintf(stderr,
        "Usage: %s <input_statsd_port> <output_collector_port> <output_statsd_port>\n"
        "Use \"\" for either of the outputs to disable them.\n", argv[0]);
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

    if (strnlen(argv[2], 100) == 0) {
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_COLLECTOR_ENABLED);
      param->set_value("false");
      LOG(INFO) << "AVRO OUT:   disabled";
    } else {
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_COLLECTOR_IP);
      param->set_value("127.0.0.1");// use 10.255.255.1 to test timeouts

      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_COLLECTOR_PORT);
      param->set_value(argv[2]);
      LOG(INFO) << "AVRO OUT:   " << param->value();
    }

    if (strnlen(argv[3], 100) == 0) {
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_STATSD_ENABLED);
      param->set_value("false");
      LOG(INFO) << "STATSD OUT: disabled";
    } else {
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_STATSD_HOST);
      param->set_value("127.0.0.1");

      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_STATSD_PORT);
      param->set_value(argv[3]);
      LOG(INFO) << "STATSD OUT: " << param->value();
    }

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

  // launch the port reader
  for (;;) {
    Try<metrics::UDPEndpoint> endpoint =
      container_assigner->register_container(container_id, executor_info);
    if (!endpoint.isError()) {
      break;
    }
    LOG(ERROR) << "Unable to register endpoint, trying again: " << endpoint.error();
  }

  // on this main thread, sleep-loop forever while any container stats come in on the port
  for (;;) {
    sleep(SLEEP_PERIOD_SECS);
  }
}
