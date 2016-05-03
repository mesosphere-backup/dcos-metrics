#include "statsd_output_writer.hpp"

typedef std::shared_ptr<stats::StatsdOutputWriter> writer_ptr_t;

class StubLookupStatsdOutputWriter : public stats::StatsdOutputWriter {
 public:

  static writer_ptr_t with_lookup_result(
      const std::string& annotation_mode,
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port,
      std::vector<boost::asio::ip::udp::endpoint> lookup_result) {
    return writer_ptr_t(new StubLookupStatsdOutputWriter(annotation_mode, io_service, port,
            lookup_result, boost::system::error_code()));
  }

  static writer_ptr_t with_lookup_result_chunking(
      const std::string& annotation_mode,
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port,
      std::vector<boost::asio::ip::udp::endpoint> lookup_result,
      size_t chunk_size,
      size_t chunk_timeout_ms) {
    return writer_ptr_t(new StubLookupStatsdOutputWriter(annotation_mode, io_service, port,
            lookup_result, boost::system::error_code(),
            chunk_size, chunk_timeout_ms));
  }

  static writer_ptr_t with_lookup_error(
      const std::string& annotation_mode,
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port,
      boost::system::error_code lookup_error) {
    return writer_ptr_t(new StubLookupStatsdOutputWriter(annotation_mode, io_service, port,
            std::vector<boost::asio::ip::udp::endpoint>(), lookup_error));
  }

  static writer_ptr_t without_chunking(
      const std::string& annotation_mode,
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port) {
    std::vector<boost::asio::ip::udp::endpoint> endpoints;
    endpoints.push_back(boost::asio::ip::udp::endpoint(
            boost::asio::ip::address::from_string("127.0.0.1"), 0 /* port */));
    return writer_ptr_t(new StubLookupStatsdOutputWriter(annotation_mode, io_service, port,
            endpoints, boost::system::error_code()));
  }

  static writer_ptr_t with_chunk_size(
      const std::string& annotation_mode,
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port,
      size_t chunk_size,
      size_t chunk_timeout_ms) {
    std::vector<boost::asio::ip::udp::endpoint> endpoints;
    endpoints.push_back(boost::asio::ip::udp::endpoint(
            boost::asio::ip::address::from_string("127.0.0.1"), 0 /* port */));
    return writer_ptr_t(new StubLookupStatsdOutputWriter(annotation_mode, io_service, port,
            endpoints, boost::system::error_code(),
            chunk_size, chunk_timeout_ms));
  }

  virtual ~StubLookupStatsdOutputWriter() {
    // cancel timers in parent class before we get destroyed:
    // ensure their timers don't call OUR resolve() after we're destroyed
    shutdown();
  }

 protected:
  udp_resolver_t::iterator resolve(boost::system::error_code& ec) {
    if (lookup_error) {
      ec = lookup_error;
      return udp_resolver_t::iterator();
    } else if (lookup_result.empty()) {
      return udp_resolver_t::iterator();
    } else {
      std::vector<boost::asio::ip::udp::endpoint> shuffled(lookup_result);
      std::random_shuffle(shuffled.begin(), shuffled.end());
#if BOOST_VERSION >= 105500
      // >=1.55.0 supports passing iterator directly:
      return udp_resolver_t::iterator::create(
          shuffled.begin(), shuffled.end(), std::string(dest_hostname), "");
#else
      // 1.54.0 and older require getaddrinfo-formatted entries:
      std::vector<addrinfo> lookup_conv(shuffled.size());
      for (size_t i = 0; i < shuffled.size(); ++i) {
        addrinfo& out = lookup_conv[i];
        out.ai_flags = 0;
        out.ai_family = AF_INET;
        out.ai_socktype = SOCK_DGRAM;
        out.ai_protocol = 0;
        out.ai_addrlen = sizeof(sockaddr_in);
        out.ai_addr = shuffled[i].data();
        out.ai_canonname = NULL;
        if ((i + 1) < shuffled.size()) {
          out.ai_next = &lookup_conv[i+1];
        } else {
          out.ai_next = NULL;
        }
      }
      return udp_resolver_t::iterator::create(&lookup_conv[0], std::string(dest_hostname), "");
#endif
    }
  }

 private:
  StubLookupStatsdOutputWriter(
      const std::string& annotation_mode,
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port,
      std::vector<boost::asio::ip::udp::endpoint> lookup_result,
      boost::system::error_code lookup_error,
      size_t chunk_size = 0,
      size_t chunk_timeout_ms = stats::StatsdOutputWriter::DEFAULT_CHUNK_TIMEOUT_MS)
    : StatsdOutputWriter(
        io_service,
        build_params(annotation_mode, "fakehost", port, chunk_size),
        chunk_timeout_ms,
        1 /* resolve_period_ms */),
      io_service(io_service),
      lookup_result(lookup_result),
      lookup_error(lookup_error),
      dest_hostname("fakehost") { }

  static mesos::Parameters build_params(
      const std::string& annotation_mode, const std::string& host, size_t port, size_t chunk_size) {
    mesos::Parameters params;
    mesos::Parameter* param;
    param = params.add_parameter();
    param->set_key(stats::params::ANNOTATION_MODE);
    param->set_value(annotation_mode);
    if (chunk_size > 0) {
      param = params.add_parameter();
      param->set_key(stats::params::CHUNKING);
      param->set_value("true");
      param = params.add_parameter();
      param->set_key(stats::params::CHUNK_SIZE_BYTES);
      param->set_value(std::to_string(chunk_size));
    } else {
      param = params.add_parameter();
      param->set_key(stats::params::CHUNKING);
      param->set_value("false");
    }
    param = params.add_parameter();
    param->set_key(stats::params::DEST_HOST);
    param->set_value(host);
    param = params.add_parameter();
    param->set_key(stats::params::DEST_PORT);
    param->set_value(std::to_string(port));
    return params;
  }


  const std::shared_ptr<boost::asio::io_service> io_service;
  const std::vector<boost::asio::ip::udp::endpoint> lookup_result;
  const boost::system::error_code lookup_error;
  const std::string dest_hostname;
};
