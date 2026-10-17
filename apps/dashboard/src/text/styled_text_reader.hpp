#pragma once
#include "graphics/surface.hpp"
#include "text/utf8.hpp"
#include <optional>
namespace kafel {
namespace styled_detail {
inline int hex(char c) noexcept {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
inline bool has_close(std::string_view text) noexcept {
    while(!text.empty()) {
        if(text.starts_with("[[")) text.remove_prefix(2);
        else if(text.starts_with("[/]")) return true;
        else text.remove_prefix(1);
    }
    return false;
}
}
struct StyledCharacter { char32_t glyph; Pixel color; };
class StyledTextReader {
public:
    StyledTextReader(std::string_view text,Pixel color) : text_(text),default_(color),color_(color) {}
    std::optional<StyledCharacter> next() noexcept {
        while(!text_.empty()) {
            if(text_.starts_with("[[")) { text_.remove_prefix(2); return StyledCharacter{U'[',color_}; }
            if(active_ && text_.starts_with("[/]")) {
                text_.remove_prefix(3); color_=default_; active_=false; continue;
            }
            if(!active_ && text_.size()>=9 && text_.starts_with("[#") && text_[8]==']') {
                unsigned value=0; bool valid=true;
                for(std::size_t i=2;i<8;++i) {
                    const int digit=styled_detail::hex(text_[i]); if(digit<0) { valid=false; break; }
                    value=(value<<4)|static_cast<unsigned>(digit);
                }
                // An unclosed opener stays readable as ordinary text.
                if(valid && styled_detail::has_close(text_.substr(9))) {
                    color_=rgb((value>>16)&255,(value>>8)&255,value&255);
                    active_=true; text_.remove_prefix(9); continue;
                }
            }
            return StyledCharacter{utf8::next(text_).codepoint,color_};
        }
        return {};
    }
private:
    std::string_view text_;
    Pixel default_,color_;
    bool active_=false;
};
}
