#include "input_state_cache_impl.hpp"

#include <stout/fs.hpp>
#include <stout/json.hpp>
#include <stout/path.hpp>

/*
 * dir format:
 * DIR/
 *  containers/
 *    container_id-0.json
 *    container_id-1.json
 *    container_id-2.json
 *    ...
 *
 * file format in container jsons:
 *  { "container_id": "container_id-0",
 *    "statsd_host": "some_host",
 *    "statsd_port": some_port }
 */

namespace {
  const std::string CONTAINER_CACHE_DIR("containers");

  const std::string CONTAINER_ID_KEY("container_id");
  const std::string HOST_KEY("statsd_host");
  const std::string PORT_KEY("statsd_port");

  /**
   * Avoid eg:
   * - malicious: "/example/path/" + "../../etc/shadow"
   * - nested: "/example/path/" + "valid-container-id/with-slash"
   * Solution: Just strip out all slashes from the name.
   */
  std::string sanitized_filename(const mesos::ContainerID& container_id) {
    return strings::remove(container_id.value(), "/");
  }
}

stats::InputStateCacheImpl::InputStateCacheImpl(const mesos::Parameters& parameters)
  : config_state_dir(params::get_str(parameters, params::STATE_PATH_DIR, params::STATE_PATH_DIR_DEFAULT)),
    container_state_dir(path::join(config_state_dir, CONTAINER_CACHE_DIR)) { }

const std::string& stats::InputStateCacheImpl::path() const {
  return config_state_dir;
}

stats::container_id_map<stats::UDPEndpoint> stats::InputStateCacheImpl::get_containers() {
  Try<std::list<std::string>> files = os::ls(container_state_dir);
  container_id_map<UDPEndpoint> map;
  if (files.isError()) {
    LOG(ERROR) << "Unable to list content of cache dir[" << container_state_dir << "]: "
               << files.error();
    return map;
  }
  for (const std::string& filename : files.get()) {
    std::string pathstr(path::join(container_state_dir, filename));
    Try<std::string> content = os::read(pathstr);
    if (content.isError()) {
      LOG(ERROR) << "Unable to read content of cache file[" << pathstr << "]: " << content.error();
      continue;
    }

    Try<JSON::Object> content_json = JSON::parse<JSON::Object>(content.get());
    if (content_json.isError()) {
      LOG(ERROR) << "Unable to parse JSON in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]: " << content_json.error();
      continue;
    }

    Result<JSON::String> container_id_json =
      content_json.get().find<JSON::String>(CONTAINER_ID_KEY);
    if (container_id_json.isError()) {
      LOG(ERROR) << "Unable to parse container id value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]: " << container_id_json.error();
      continue;
    } else if (container_id_json.isNone()) {
      LOG(ERROR) << "Missing container_id value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]";
      continue;
    }

    Result<JSON::String> host = content_json.get().find<JSON::String>(HOST_KEY);
    if (host.isError()) {
      LOG(ERROR) << "Unable to parse host value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]: " << host.error();
      continue;
    } else if (host.isNone()) {
      LOG(ERROR) << "Missing host value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]";
      continue;
    }

    Result<JSON::Number> port = content_json.get().find<JSON::Number>(PORT_KEY);
    if (port.isError()) {
      LOG(ERROR) << "Unable to parse port value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]: " << port.error();
      continue;
    } else if (port.isNone()) {
      LOG(ERROR) << "Missing port value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "]";
      continue;
    } else if (port.get().as<long>() < 0) {
      LOG(ERROR) << "Port value in cache file[" << pathstr << "] "
                 << "content[" << content.get() << "] must be non-negative";
      continue;
    }

    mesos::ContainerID container_id;
    container_id.set_value(container_id_json.get().value);
    stats::UDPEndpoint endpoint(host.get().value, port.get().as<size_t>());

    LOG(INFO) << "Found container file[" << pathstr << "] with "
              << "container_id[" << container_id.value() << "] => "
              << "endpoint[" << endpoint.string() << "]";

    map.insert(std::make_pair(container_id, endpoint));
  }
  return map;
}

void stats::InputStateCacheImpl::add_container(
    const mesos::ContainerID& container_id, const UDPEndpoint& endpoint) {
  if (!os::exists(container_state_dir)) {
    LOG(INFO) << "Creating new container state directory[" << container_state_dir << "]";
    Try<Nothing> result = os::mkdir(container_state_dir);
    if (result.isError()) {
      LOG(ERROR) << "Failed to create container state directory[" << container_state_dir << "]: " << result.error();
      return;
    }
  }

  std::string container_path = path::join(container_state_dir, sanitized_filename(container_id));
  LOG(INFO) << "Writing container file[" << container_path << "] with "
            << "endpoint[" << endpoint.string() << "]";
  JSON::Object json_obj;
  json_obj.values[CONTAINER_ID_KEY] = container_id.value();
  json_obj.values[HOST_KEY] = endpoint.host;
  json_obj.values[PORT_KEY] = endpoint.port;
  Try<Nothing> result = os::write(container_path, stringify(json_obj));
}

void stats::InputStateCacheImpl::remove_container(const mesos::ContainerID& container_id) {
  std::string container_path = path::join(container_state_dir, sanitized_filename(container_id));
  LOG(INFO) << "Removing container file[" << container_path << "]";
  Try<Nothing> result = os::rm(container_path);
  if (result.isError()) {
    LOG(ERROR) << "Failed to remove container file[" << container_path << "]: " << result.error();
  }
}
