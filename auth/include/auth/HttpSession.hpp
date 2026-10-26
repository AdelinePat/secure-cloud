#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include "auth/HttpRouter.hpp"

namespace auth {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class HttpSession {
 public:
  HttpSession(tcp::socket socket, HttpRouter& router);

  void run();

 private:
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;

  http::request<http::string_body> request_;
  http::response<http::string_body> response_;

  HttpRouter& router_;
};

}  // namespace auth