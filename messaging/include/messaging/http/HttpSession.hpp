#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include "messaging/http/HttpRouter.hpp"

namespace messaging::http {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

class HttpSession {
 public:
  HttpSession(asio::ip::tcp::socket socket, HttpRouter& router);

  void run();

 private:
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;

  http::request<http::string_body> request_;
  http::response<http::string_body> response_;

  HttpRouter& router_;
};

}  // namespace messaging::http
