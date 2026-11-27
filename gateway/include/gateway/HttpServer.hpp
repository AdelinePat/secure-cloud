#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "gateway/HttpClient.hpp"
#include "gateway/HttpRouter.hpp"

namespace gateway {

// PEM files for the public HTTPS listener.
struct TlsFiles {
  std::string certificateChain;  // server certificate (optionally + chain)
  std::string privateKey;
};

class HttpServer {
 public:
  // Without `tls` the gateway speaks plain HTTP (tests, local tooling).
  // With it, every connection must complete a TLS 1.2+ handshake first;
  // throws if the files cannot be loaded or do not match.
  HttpServer(boost::asio::io_context& ioContext, std::uint16_t port,
             std::optional<TlsFiles> tls = std::nullopt);
  std::size_t acceptedConnections() const;

  void start();
  std::uint16_t localPort() const;
  bool tlsEnabled() const { return sslContext_ != nullptr; }

 private:
  void acceptConnection();
  std::size_t acceptedConnections_ = 0;

  boost::asio::io_context& ioContext_;
  std::uint16_t port_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::unique_ptr<boost::asio::ssl::context> sslContext_;

  HttpClient httpClient_;
  HttpRouter router_;
};

}  // namespace gateway
