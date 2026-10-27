#pragma once
#include "document.hpp"
#include "text/utf8.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
namespace kafel::json {
using Json = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
#ifdef KAFEL_JSON_TEST_STATS
// Test instrumentation only; no counters or TLS access in production builds.
inline thread_local std::size_t parsed_documents = 0;
#endif
namespace strict_detail {
[[noreturn]] inline void fail(const char *code) { throw std::invalid_argument(code); }
} // namespace strict_detail
inline Json parse(std::string_view input, unsigned maximum_depth = 16) {
#ifdef KAFEL_JSON_TEST_STATS
    ++parsed_documents;
#endif
    auto root = document(input, 65536, maximum_depth);
    if (!root)
        strict_detail::fail("invalid_message");
    return *root;
}
inline void shape(const Json &j, std::initializer_list<std::string_view> required,
                  std::initializer_list<std::string_view> optional = {}) {
    if (!j.is<Object>())
        strict_detail::fail("invalid_message");
    for (auto k : required)
        if (!j.contains(std::string(k)))
            strict_detail::fail("invalid_message");
    for (const auto &[k, v] : j.get<Object>()) {
        (void)v;
        if (std::find(required.begin(), required.end(), k) == required.end() &&
            std::find(optional.begin(), optional.end(), k) == optional.end())
            strict_detail::fail("invalid_message");
    }
}
inline std::string_view text_view(const Json &j, std::size_t max, bool empty = false) {
    if (!j.is<std::string>())
        strict_detail::fail("invalid_message");
    const auto &s = j.get<std::string>();
    if (s.size() > max || (!empty && s.empty()) || !kafel::utf8::valid(s, true))
        strict_detail::fail("invalid_message");
    return s;
}
inline std::string text(const Json &value, std::size_t maximum, bool empty = false) {
    return std::string(text_view(value, maximum, empty));
}
inline std::string identifier(const Json &j) {
    auto s = text(j, 32);
    if (s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != s.npos)
        strict_detail::fail("invalid_message");
    return s;
}
inline std::uint64_t integer(const Json &j, std::uint64_t min, std::uint64_t max) {
    if (!j.is<double>())
        strict_detail::fail("invalid_message");
    double n = j.get<double>();
    if (!std::isfinite(n) || n < min || n > max || std::floor(n) != n)
        strict_detail::fail("invalid_message");
    return static_cast<std::uint64_t>(n);
}
} // namespace kafel::json
