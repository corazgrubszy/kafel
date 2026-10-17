#pragma once
#include <string_view>
namespace kafel {
// Unicode LINE SEPARATOR keeps both rows in one atomic DASH/4 text field.
struct TextRows {
    std::string_view main,caption;
    bool stacked=false;
    static constexpr std::string_view separator="\u2028";
    static constexpr TextRows split(std::string_view text) noexcept {
        const auto position=text.find(separator);
        if(position==std::string_view::npos) return {text,{},false};
        return {text.substr(0,position),text.substr(position+separator.size()),true};
    }
    static constexpr bool valid(std::string_view text) noexcept {
        return split(text).caption.find(separator)==std::string_view::npos;
    }
};
}
