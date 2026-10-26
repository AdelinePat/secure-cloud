#pragma once

#include <cstddef>
#include <string>

namespace audit::http {

struct HttpConfig {
  std::string host = "0.0.0.0";
  int port = 8085;

  // When true the server is HTTPS and requires a client certificate signed by
  // `ca_path` (mutual TLS). The peer CN becomes the caller identity.
  bool tls_enabled = false;
  std::string cert_path;
  std::string key_path;
  std::string ca_path;

  std::size_t max_payload_bytes = 1 << 20;  // 1 MiB
};

}  // namespace audit::http
