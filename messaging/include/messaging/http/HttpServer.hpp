#pragma once

#include <boost/asio.hpp>
#include <cstdint>

#include "messaging/http/HttpRouter.hpp"

namespace messaging::http {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class HttpServer {
 public:
  HttpServer(asio::io_context& ioContext, std::uint16_t port,
             HttpRouter& router);

  // Blocking accept loop: one request per connection, matching the gateway's
  // keep-alive=false behaviour.
  void run();

 private:
  tcp::acceptor acceptor_;
  HttpRouter& router_;
};

}  // namespace messaging::http
