#include "socket_sender.hpp"

template <typename AsioProtocol>
class StubSocketSender : public metrics::SocketSender<AsioProtocol> {
 public:

  typedef std::shared_ptr<StubSocketSender<AsioProtocol>> ptr_t;

  static ptr_t custom_success(std::shared_ptr<boost::asio::io_service> io_service, size_t port,
      std::vector<typename AsioProtocol::endpoint> lookup_result) {
    return ptr_t(new StubSocketSender<AsioProtocol>(
            io_service, port, lookup_result, boost::system::error_code()));
  }

  static ptr_t error(std::shared_ptr<boost::asio::io_service> io_service, size_t port,
      boost::system::error_code lookup_error) {
    return ptr_t(new StubSocketSender<AsioProtocol>(
            io_service, port, std::vector<typename AsioProtocol::endpoint>(), lookup_error));
  }

  static ptr_t success(std::shared_ptr<boost::asio::io_service> io_service, size_t port) {
    std::vector<typename AsioProtocol::endpoint> endpoints;
    endpoints.push_back(typename AsioProtocol::endpoint(
            boost::asio::ip::address::from_string("127.0.0.1"), 0 /* port */));
    return custom_success(io_service, port, endpoints);
  }

  static ptr_t empty(std::shared_ptr<boost::asio::io_service> io_service, size_t port) {
    return custom_success(io_service, port, std::vector<typename AsioProtocol::endpoint>());
  }

  virtual ~StubSocketSender() {
    // cancel timers in parent class before we get destroyed:
    // ensure their timers don't call OUR resolve() after we're destroyed
    this->shutdown();
  }

 protected:
  typename AsioProtocol::resolver::iterator resolve(boost::system::error_code& ec) {
    if (lookup_error) {
      ec = lookup_error;
      return typename AsioProtocol::resolver::iterator();
    } else if (lookup_result.empty()) {
      return typename AsioProtocol::resolver::iterator();
    } else {
      std::vector<typename AsioProtocol::endpoint> shuffled(lookup_result);
      std::random_shuffle(shuffled.begin(), shuffled.end());
#if BOOST_VERSION >= 105500
      // >=1.55.0 supports passing iterator directly:
      return AsioProtocol::resolver::iterator::create(
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
      return AsioProtocol::resolver::iterator::create(
          &lookup_conv[0], std::string(dest_hostname), "");
#endif
    }
  }

 private:
  StubSocketSender(
      std::shared_ptr<boost::asio::io_service> io_service,
      size_t port,
      std::vector<typename AsioProtocol::endpoint> lookup_result,
      boost::system::error_code lookup_error)
    : metrics::SocketSender<AsioProtocol>(
        io_service, "fakehost", port, 1 /* resolve_period_ms */),
      io_service(io_service),
      lookup_result(lookup_result),
      lookup_error(lookup_error),
      dest_hostname("fakehost") { }

  const std::shared_ptr<boost::asio::io_service> io_service;
  const std::vector<typename AsioProtocol::endpoint> lookup_result;
  const boost::system::error_code lookup_error;
  const std::string dest_hostname;
};
