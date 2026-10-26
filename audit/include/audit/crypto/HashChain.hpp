#pragma once

#include <string>
#include <string_view>

#include "audit/domain/AuditEvent.hpp"

namespace audit::crypto {

// prev_hash of seq 1. Fixed width so every chain link has the same shape.
inline constexpr std::string_view kGenesisHash =
    "0000000000000000000000000000000000000000000000000000000000000000";

std::string sha256Hex(std::string_view data);

// Length-prefixed, field-ordered serialisation of an event (everything except
// `hash`). Length prefixes remove the ambiguity of delimiter-based encodings,
// so two different field combinations can never produce the same payload.
std::string canonicalPayload(const domain::AuditEvent& event);

std::string computeHash(const domain::AuditEvent& event);

}  // namespace audit::crypto
