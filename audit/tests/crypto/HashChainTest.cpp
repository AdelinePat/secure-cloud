#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>

#include "audit/crypto/HashChain.hpp"

namespace {

audit::domain::AuditEvent makeEvent() {
  audit::domain::AuditEvent event;
  event.id = "6c1f9f0e-3d6a-4a0e-9a0e-2b7a3c9d1e2f";
  event.seq = 1;
  event.event_type = "file_uploaded";
  event.service_name = "file";
  event.actor_id = "user-1";
  event.target_id = "file-1";
  event.outcome = "success";
  event.occurred_at = "2026-10-21T09:00:00.000Z";
  event.recorded_at = "2026-10-21T09:00:01.000Z";
  event.metadata_json = R"({"size":42})";
  event.prev_hash = std::string(audit::crypto::kGenesisHash);
  return event;
}

}  // namespace

TEST(HashChainTest, ProducesLowercaseSha256) {
  const auto hash = audit::crypto::computeHash(makeEvent());
  ASSERT_EQ(hash.size(), 64u);
  EXPECT_TRUE(std::all_of(hash.begin(), hash.end(), [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0 || (c >= 'a' && c <= 'f');
  }));
}

TEST(HashChainTest, IsDeterministic) {
  EXPECT_EQ(audit::crypto::computeHash(makeEvent()),
            audit::crypto::computeHash(makeEvent()));
}

TEST(HashChainTest, AnyFieldChangeChangesTheHash) {
  const auto baseline = audit::crypto::computeHash(makeEvent());

  auto changed = makeEvent();
  changed.outcome = "failure";
  EXPECT_NE(baseline, audit::crypto::computeHash(changed));

  changed = makeEvent();
  changed.metadata_json = R"({"size":43})";
  EXPECT_NE(baseline, audit::crypto::computeHash(changed));

  changed = makeEvent();
  changed.prev_hash = std::string(64, 'a');
  EXPECT_NE(baseline, audit::crypto::computeHash(changed));

  changed = makeEvent();
  changed.actor_id = std::nullopt;
  EXPECT_NE(baseline, audit::crypto::computeHash(changed));
}

TEST(HashChainTest, FieldBoundariesAreUnambiguous) {
  auto first = makeEvent();
  first.event_type = "ab";
  first.service_name = "c";

  auto second = makeEvent();
  second.event_type = "a";
  second.service_name = "bc";

  EXPECT_NE(audit::crypto::computeHash(first),
            audit::crypto::computeHash(second));
}

TEST(HashChainTest, GenesisHashIsAFullWidthZeroHash) {
  EXPECT_EQ(audit::crypto::kGenesisHash.size(), 64u);
  EXPECT_EQ(audit::crypto::kGenesisHash,
            "0000000000000000000000000000000000000000000000000000000000000000");
}
