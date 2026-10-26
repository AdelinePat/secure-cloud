#include "audit/crypto/HashChain.hpp"

#include <openssl/evp.h>

#include <cstdio>
#include <memory>
#include <stdexcept>

namespace audit::crypto {
namespace {

void appendField(std::string& out, std::string_view value) {
  out += std::to_string(value.size());
  out += ':';
  out += value;
}

void appendOptional(std::string& out, const std::optional<std::string>& value) {
  if (!value.has_value()) {
    out += 'n';
    return;
  }
  out += 's';
  appendField(out, *value);
}

}  // namespace

std::string sha256Hex(std::string_view data) {
  using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  Context context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context) {
    throw std::runtime_error("EVP_MD_CTX_new failed");
  }
  if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context.get(), data.data(), data.size()) != 1) {
    throw std::runtime_error("sha256 init/update failed");
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1) {
    throw std::runtime_error("sha256 final failed");
  }

  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(length * 2);
  for (unsigned int i = 0; i < length; ++i) {
    hex += kHex[digest[i] >> 4];
    hex += kHex[digest[i] & 0x0F];
  }
  return hex;
}

std::string canonicalPayload(const domain::AuditEvent& event) {
  std::string payload;
  payload.reserve(256 + event.metadata_json.size());

  appendField(payload, "audit-event-v1");
  appendField(payload, event.id);
  appendField(payload, std::to_string(event.seq));
  appendField(payload, event.event_type);
  appendField(payload, event.service_name);
  appendOptional(payload, event.actor_id);
  appendOptional(payload, event.target_id);
  appendField(payload, event.outcome);
  appendField(payload, event.occurred_at);
  appendField(payload, event.recorded_at);
  appendField(payload, event.metadata_json);
  appendField(payload, event.prev_hash);
  return payload;
}

std::string computeHash(const domain::AuditEvent& event) {
  return sha256Hex(canonicalPayload(event));
}

}  // namespace audit::crypto
