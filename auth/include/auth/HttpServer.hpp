#pragma once

#include <boost/asio.hpp>
#include <cstdint>

#include "auth/HttpRouter.hpp"

namespace auth {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class HttpServer {
 public:
  HttpServer(asio::io_context& ioContext, std::uint16_t port,
             services::AuthService& authService);

  void run();

 private:
  tcp::acceptor acceptor_;
  HttpRouter router_;
};

}  // namespace auth