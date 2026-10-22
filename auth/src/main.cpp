#include <boost/asio.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "auth/HttpServer.hpp"

int main() {
  try {
    const char* envPort = std::getenv("AUTH_PORT");

    const std::uint16_t port =
        envPort ? static_cast<std::uint16_t>(std::stoi(envPort)) : 8081;

    boost::asio::io_context ioContext;

    auth::HttpServer server(ioContext, port);

    std::cout << "Auth service listening on port " << port << std::endl;

    server.run();

  } catch (const std::exception& exception) {
    std::cerr << "Auth server error: " << exception.what() << std::endl;
    return 1;
  }

  return 0;
}