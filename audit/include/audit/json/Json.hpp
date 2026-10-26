#pragma once

#include <bson/bson.h>

#include <optional>
#include <string>
#include <string_view>

namespace audit::json {

// Parses `input`, requires a JSON object, and returns a canonical form:
// compact, object keys sorted, no insignificant whitespace. Canonicalisation
// makes the hash chain independent of key order and formatting in the request.
//
// Returns std::nullopt and fills `error` for invalid JSON, non-object input,
// duplicate keys, or values that cannot be stored in BSON.
std::optional<std::string> canonicalizeObject(std::string_view input,
                                              std::string& error);

// Canonical form of an already-parsed BSON document. Used on read so that the
// hash can be recomputed from what the database actually stored.
std::string canonicalizeDocument(const bson_t& document);

// JSON string literal (including surrounding quotes), with control characters
// and quotes escaped. UTF-8 bytes are passed through unchanged.
std::string quote(std::string_view value);

}  // namespace audit::json
