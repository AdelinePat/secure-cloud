#pragma once

#include <pqxx/pqxx>

#include "messaging/repositories/IMessageRepository.hpp"

namespace messaging::repositories {

class PostgresMessageRepository : public IMessageRepository {
 public:
  explicit PostgresMessageRepository(pqxx::connection& conn);

  domain::Message create(const domain::Message& message) override;
  MessagePage history(const std::string& conversationId,
                       std::optional<std::string> before, int limit) override;

 private:
  pqxx::connection& conn_;
};

}  // namespace messaging::repositories
