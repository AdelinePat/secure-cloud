#include "audit/json/Json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <vector>

#include "audit/util/Time.hpp"

namespace audit::json {
namespace {

void appendEscaped(std::string& out, std::string_view value) {
  out += '"';
  for (const char raw : value) {
    const auto c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void appendCanonicalValue(const bson_iter_t& iter, std::string& out);

void appendCanonicalDocument(const bson_t& document, std::string& out) {
  std::vector<std::string> keys;
  bson_iter_t iter;
  if (bson_iter_init(&iter, &document)) {
    while (bson_iter_next(&iter)) {
      keys.emplace_back(bson_iter_key(&iter));
    }
  }
  std::sort(keys.begin(), keys.end());
  if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
    throw std::runtime_error("duplicate object keys are not allowed");
  }

  out += '{';
  bool first = true;
  for (const std::string& key : keys) {
    bson_iter_t found;
    if (!bson_iter_init(&found, &document) || !bson_iter_find(&found, key.c_str())) {
      continue;
    }
    if (!first) out += ',';
    first = false;
    appendEscaped(out, key);
    out += ':';
    appendCanonicalValue(found, out);
  }
  out += '}';
}

void appendCanonicalArray(const bson_iter_t& iter, std::string& out) {
  const uint8_t* buffer = nullptr;
  uint32_t length = 0;
  bson_iter_array(&iter, &length, &buffer);

  bson_t array;
  if (!bson_init_static(&array, buffer, length)) {
    throw std::runtime_error("malformed BSON array");
  }

  bson_iter_t element;
  bson_iter_init(&element, &array);

  out += '[';
  bool first = true;
  while (bson_iter_next(&element)) {
    if (!first) out += ',';
    first = false;
    appendCanonicalValue(element, out);
  }
  out += ']';

  bson_destroy(&array);
}

void appendCanonicalValue(const bson_iter_t& iter, std::string& out) {
  switch (bson_iter_type(&iter)) {
    case BSON_TYPE_UTF8: {
      uint32_t length = 0;
      const char* value = bson_iter_utf8(&iter, &length);
      appendEscaped(out, std::string_view(value, length));
      return;
    }
    case BSON_TYPE_INT32:
      out += std::to_string(bson_iter_int32(&iter));
      return;
    case BSON_TYPE_INT64:
      out += std::to_string(bson_iter_int64(&iter));
      return;
    case BSON_TYPE_DOUBLE: {
      const double value = bson_iter_double(&iter);
      if (!std::isfinite(value)) {
        throw std::runtime_error("non-finite numbers are not allowed");
      }
      char buffer[64];
      const auto result =
          std::to_chars(buffer, buffer + sizeof(buffer), value,
                        std::chars_format::general);
      std::string number(buffer, result.ptr);
      // Keep the number a double on re-parse so the canonical form is stable.
      if (number.find_first_of(".eE") == std::string::npos) number += ".0";
      out += number;
      return;
    }
    case BSON_TYPE_BOOL:
      out += bson_iter_bool(&iter) ? "true" : "false";
      return;
    case BSON_TYPE_NULL:
      out += "null";
      return;
    case BSON_TYPE_DOCUMENT: {
      const uint8_t* buffer = nullptr;
      uint32_t length = 0;
      bson_iter_document(&iter, &length, &buffer);
      bson_t document;
      if (!bson_init_static(&document, buffer, length)) {
        throw std::runtime_error("malformed BSON document");
      }
      appendCanonicalDocument(document, out);
      bson_destroy(&document);
      return;
    }
    case BSON_TYPE_ARRAY:
      appendCanonicalArray(iter, out);
      return;
    case BSON_TYPE_DATE_TIME:
      appendEscaped(out, util::formatIso8601(bson_iter_date_time(&iter)));
      return;
    case BSON_TYPE_OID: {
      char oid[25];
      bson_oid_to_string(bson_iter_oid(&iter), oid);
      appendEscaped(out, oid);
      return;
    }
    default:
      throw std::runtime_error("unsupported value type in metadata");
  }
}

}  // namespace

std::optional<std::string> canonicalizeObject(std::string_view input,
                                              std::string& error) {
  const auto first = input.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos || input[first] != '{') {
    error = "metadata must be a JSON object";
    return std::nullopt;
  }

  bson_error_t bson_error{};
  bson_t* document =
      bson_new_from_json(reinterpret_cast<const uint8_t*>(input.data()),
                         static_cast<ssize_t>(input.size()), &bson_error);
  if (document == nullptr) {
    error = bson_error.message[0] != '\0' ? bson_error.message : "invalid JSON";
    return std::nullopt;
  }

  std::string canonical;
  try {
    canonical = canonicalizeDocument(*document);
  } catch (const std::exception& exception) {
    bson_destroy(document);
    error = exception.what();
    return std::nullopt;
  }
  bson_destroy(document);

  if (canonical.empty() || canonical.front() != '{') {
    error = "metadata must be a JSON object";
    return std::nullopt;
  }
  return canonical;
}

std::string canonicalizeDocument(const bson_t& document) {
  std::string out;
  appendCanonicalDocument(document, out);
  return out;
}

std::string quote(std::string_view value) {
  std::string out;
  appendEscaped(out, value);
  return out;
}

}  // namespace audit::json
