#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

#include "gateway/HttpServer.hpp"

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

using KeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using CertPtr = std::unique_ptr<X509, decltype(&X509_free)>;

KeyPtr makeKey() {
  return KeyPtr(EVP_EC_gen("P-256"), EVP_PKEY_free);
}

// Self-signed certificate for "localhost" (it is its own CA).
CertPtr makeCertificate(EVP_PKEY* key) {
  CertPtr cert(X509_new(), X509_free);
  X509_set_version(cert.get(), 2);
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600);
  X509_set_pubkey(cert.get(), key);

  X509_NAME* name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>("localhost"),
                             -1, -1, 0);
  X509_set_issuer_name(cert.get(), name);

  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, cert.get(), cert.get(), nullptr, nullptr, 0);
  for (const auto& [nid, value] :
       {std::pair{NID_subject_alt_name, "DNS:localhost"},
        std::pair{NID_basic_constraints, "critical,CA:TRUE"}}) {
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
  }
  X509_sign(cert.get(), key, EVP_sha256());
  return cert;
}

class TempDir {
 public:
  TempDir() {
    std::random_device random;
    path_ = std::filesystem::temp_directory_path() /
            ("gateway-tls-test-" + std::to_string(random()));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void writeKey(const std::filesystem::path& path, EVP_PKEY* key) {
  FILE* file = std::fopen(path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  PEM_write_PrivateKey(file, key, nullptr, nullptr, 0, nullptr, nullptr);
  std::fclose(file);
}

void writeCert(const std::filesystem::path& path, X509* cert) {
  FILE* file = std::fopen(path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  PEM_write_X509(file, cert);
  std::fclose(file);
}

class HttpServerTlsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(key_);
    writeKey(keyPath(), key_.get());
    writeCert(certPath(), cert_.get());
  }

  void TearDown() override {
    serverContext_.stop();
    if (serverThread_.joinable()) serverThread_.join();
  }

  std::filesystem::path certPath() const { return dir_.path() / "server.crt"; }
  std::filesystem::path keyPath() const { return dir_.path() / "server.key"; }

  gateway::TlsFiles files() const {
    return {certPath().string(), keyPath().string()};
  }

  void startServer() {
    server_ = std::make_unique<gateway::HttpServer>(serverContext_, 0, files());
    server_->start();
    serverThread_ = std::thread([this] { serverContext_.run(); });
  }

  tcp::endpoint endpoint() const {
    return {asio::ip::make_address("127.0.0.1"), server_->localPort()};
  }

  TempDir dir_;
  KeyPtr key_ = makeKey();
  CertPtr cert_ = makeCertificate(key_.get());
  asio::io_context serverContext_;
  std::unique_ptr<gateway::HttpServer> server_;
  std::thread serverThread_;
};

TEST_F(HttpServerTlsTest, ServesHttpsToClientTrustingTheCertificate) {
  startServer();
  EXPECT_TRUE(server_->tlsEnabled());

  ssl::context clientContext(ssl::context::tls_client);
  clientContext.load_verify_file(certPath().string());
  clientContext.set_verify_mode(ssl::verify_peer);

  asio::io_context ioContext;
  beast::ssl_stream<beast::tcp_stream> stream(ioContext, clientContext);
  ASSERT_TRUE(SSL_set_tlsext_host_name(stream.native_handle(), "localhost"));
  stream.set_verify_callback(ssl::host_name_verification("localhost"));
  beast::get_lowest_layer(stream).connect(endpoint());
  stream.handshake(ssl::stream_base::client);

  http::request<http::string_body> request{http::verb::get, "/health", 11};
  request.set(http::field::host, "localhost");
  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);
  EXPECT_EQ(response.result(), http::status::ok);

  // The gateway closes cleanly with a TLS close_notify.
  beast::error_code ec;
  stream.shutdown(ec);
  EXPECT_TRUE(!ec || ec == asio::error::eof || ec == ssl::error::stream_truncated)
      << ec.message();
}

TEST_F(HttpServerTlsTest, RejectsClientThatDoesNotTrustTheCertificate) {
  startServer();

  ssl::context clientContext(ssl::context::tls_client);
  clientContext.set_verify_mode(ssl::verify_peer);  // no CA loaded

  asio::io_context ioContext;
  beast::ssl_stream<beast::tcp_stream> stream(ioContext, clientContext);
  beast::get_lowest_layer(stream).connect(endpoint());

  beast::error_code ec;
  stream.handshake(ssl::stream_base::client, ec);
  EXPECT_TRUE(ec);
}

TEST_F(HttpServerTlsTest, DoesNotAnswerPlainHttp) {
  startServer();

  asio::io_context ioContext;
  beast::tcp_stream stream(ioContext);
  stream.connect(endpoint());

  http::request<http::string_body> request{http::verb::get, "/health", 11};
  request.set(http::field::host, "localhost");
  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  beast::error_code ec;
  stream.expires_after(std::chrono::seconds(5));
  http::read(stream, buffer, response, ec);
  EXPECT_TRUE(ec);  // no HTTP response, the handshake just fails
}

TEST_F(HttpServerTlsTest, RefusesKeyThatDoesNotMatchTheCertificate) {
  KeyPtr otherKey = makeKey();
  writeKey(keyPath(), otherKey.get());
  EXPECT_THROW(gateway::HttpServer(serverContext_, 0, files()),
               std::exception);
}

TEST_F(HttpServerTlsTest, RefusesMissingFiles) {
  EXPECT_THROW(gateway::HttpServer(serverContext_, 0,
                                   gateway::TlsFiles{"/nonexistent.crt",
                                                     keyPath().string()}),
               std::exception);
}

TEST(HttpServerPlainTest, TlsIsOffByDefault) {
  asio::io_context ioContext;
  gateway::HttpServer server(ioContext, 0);
  EXPECT_FALSE(server.tlsEnabled());
}

}  // namespace
