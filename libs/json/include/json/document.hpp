#pragma once
#include "picojson.h"
#include "text/utf8.hpp"
#include <optional>
#include <stdexcept>
#include <string_view>
namespace kafel::json {
namespace document_detail {
// Build the DOM while checking depth, decoded key uniqueness and Unicode.
// Override recursive callbacks so every child uses this same context.
class Context : public picojson::default_parse_context {
    picojson::value &output_;
    unsigned depth_, maximum_depth_;

  public:
    Context(picojson::value &output, unsigned depth, unsigned maximum_depth)
        : picojson::default_parse_context(&output), output_(output), depth_(depth),
          maximum_depth_(maximum_depth) {}
    template <class Iterator> bool parse_string(picojson::input<Iterator> &input) {
        return picojson::default_parse_context::parse_string(input) &&
               kafel::utf8::valid(output_.get<std::string>());
    }
    bool parse_array_start() {
        return depth_ < maximum_depth_ && picojson::default_parse_context::parse_array_start();
    }
    bool parse_object_start() {
        return depth_ < maximum_depth_ && picojson::default_parse_context::parse_object_start();
    }
    template <class Iterator> bool parse_array_item(picojson::input<Iterator> &input, std::size_t) {
        auto &array = output_.get<picojson::array>();
        array.emplace_back();
        Context child(array.back(), depth_ + 1, maximum_depth_);
        return picojson::_parse(child, input);
    }
    template <class Iterator>
    bool parse_object_item(picojson::input<Iterator> &input, const std::string &key) {
        if (!kafel::utf8::valid(key))
            return false;
        auto &object = output_.get<picojson::object>();
        auto [entry, inserted] = object.emplace(key, picojson::value{});
        if (!inserted)
            return false;
        Context child(entry->second, depth_ + 1, maximum_depth_);
        return picojson::_parse(child, input);
    }
};
} // namespace document_detail
// One bounded pass; no shadow parser for duplicate keys or second Unicode walk.
inline std::optional<picojson::value> document(std::string_view input, std::size_t maximum = 65536,
                                               unsigned maximum_depth = 32) {
    if (input.size() > maximum)
        return {};
    try {
        picojson::value root;
        document_detail::Context context(root, 0, maximum_depth);
        std::string error;
        auto end = picojson::_parse(context, input.begin(), input.end(), &error);
        while (end != input.end() && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
            ++end;
        if (!error.empty() || end != input.end())
            return {};
        return root;
    } catch (const std::overflow_error &) {
        return {};
    }
}
} // namespace kafel::json
