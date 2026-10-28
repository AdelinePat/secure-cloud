#pragma once

#include <set>
#include <string>

#include "messaging/repositories/IUserDirectory.hpp"

namespace messaging::repositories::test {

class InMemoryUserDirectory : public IUserDirectory {
 public:
  void add(const std::string& userId) { users_.insert(userId); }

  bool exists(const std::string& userId) override {
    return users_.count(userId) > 0;
  }

 private:
  std::set<std::string> users_;
};

}  // namespace messaging::repositories::test
