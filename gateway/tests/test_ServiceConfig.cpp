#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "gateway/ServiceConfig.hpp"

TEST(ServiceConfigTests, ReturnsValidAuthConfiguration) {
  // Define a valid service configuration
  setenv("AUTH_HOST", "auth", 1);
  setenv("AUTH_PORT", "8081", 1);

  const auto endpoint = gateway::ServiceConfig::auth();

  EXPECT_EQ(endpoint.host, "auth");
  EXPECT_EQ(endpoint.port, 8081);
}

TEST(ServiceConfigTests, ThrowsWhenAuthHostIsMissing) {
  // Remove the host environment variable
  unsetenv("AUTH_HOST");
  setenv("AUTH_PORT", "8081", 1);

  EXPECT_THROW(gateway::ServiceConfig::auth(), std::runtime_error);
}

TEST(ServiceConfigTests, ThrowsWhenAuthPortIsInvalid) {
  // Define an invalid TCP port
  setenv("AUTH_HOST", "auth", 1);
  setenv("AUTH_PORT", "99999", 1);

  EXPECT_THROW(gateway::ServiceConfig::auth(), std::runtime_error);
}

TEST(ServiceConfigTests, ReturnsValidMessagingConfiguration) {
  setenv("MESSAGING_HOST", "messaging", 1);
  setenv("MESSAGING_PORT", "8082", 1);

  const auto endpoint = gateway::ServiceConfig::messaging();

  EXPECT_EQ(endpoint.host, "messaging");
  EXPECT_EQ(endpoint.port, 8082);
}

TEST(ServiceConfigTests, ThrowsWhenMessagingHostIsMissing) {
  unsetenv("MESSAGING_HOST");
  setenv("MESSAGING_PORT", "8082", 1);

  EXPECT_THROW(gateway::ServiceConfig::messaging(), std::runtime_error);
}

TEST(ServiceConfigTests, ThrowsWhenMessagingPortIsInvalid) {
  setenv("MESSAGING_HOST", "messaging", 1);
  setenv("MESSAGING_PORT", "not-a-port", 1);

  EXPECT_THROW(gateway::ServiceConfig::messaging(), std::runtime_error);
}