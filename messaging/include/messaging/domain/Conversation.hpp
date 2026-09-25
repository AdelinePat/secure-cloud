#pragma once

#include <optional>
#include <string>
#include <vector>

namespace messaging::domain {

struct Participant {
  std::string user_id;
  std::string role;        // "admin" | "member"
  std::string joined_at;   // ISO8601
};

struct Conversation {
  std::string id;                        // empty when passed to create()
  std::optional<std::string> creator_id;
  std::string type;                      // "direct" | "group"
  std::optional<std::string> name;       // nullopt for direct, required for group
  std::string created_at;
  std::string updated_at;

  // On create(): the participants to insert (including the creator).
  // On read (findById/listForUser): populated by the store.
  std::vector<Participant> participants;
};

}  // namespace messaging::domain
