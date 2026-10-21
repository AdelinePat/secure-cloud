#pragma once

#include <boost/asio.hpp>

#include <cstdint>
#include <cstddef>

namespace gateway {

class HttpServer {
 public:
  HttpServer(boost::asio::io_context& ioContext, std::uint16_t port);
  std::size_t acceptedConnections() const;

  void start();
  std::uint16_t localPort() const;

 private:
 void acceptConnection();
 std::size_t acceptedConnections_ = 0;

  boost::asio::io_context& ioContext_;
  std::uint16_t port_;
  boost::asio::ip::tcp::acceptor acceptor_;
};

}  // namespace gateway