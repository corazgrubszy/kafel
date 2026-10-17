#pragma once
#include "surface.hpp"
#include "theme.hpp"
#include <memory>
#include <span>
#include <string_view>
namespace ui {
// Trusted bundled TTF only. Prepare the exact repertoire and sizes up front.
// Implementation types and stb stay outside public headers.
class TextFont {
public:
    enum class MissingGlyph { Reject, QuestionMark };
    TextFont(const char* path,std::string_view repertoire,std::span<const TextStyle> styles);
    TextFont(std::span<const unsigned char> bytes,std::string_view repertoire,
             std::span<const TextStyle> styles,MissingGlyph policy=MissingGlyph::Reject);
    ~TextFont();
    TextFont(const TextFont&)=delete;
    TextFont& operator=(const TextFont&)=delete;
    float measure(std::string_view text,TextStyle style) const;
    void draw(Surface& surface,PixelRect clip,int x,int top,std::string_view text,TextStyle style) const;
private:
    void prepare(std::string_view repertoire,std::span<const TextStyle> styles);
    struct Impl; std::unique_ptr<Impl> impl_;
};
// Original extended geometric digits from the approved mockup, not a TTF.
// Masks are prepared once; clock and metric values use the same outlines.
class NumberFont {
public:
    explicit NumberFont(std::span<const NumberStyle> styles);
    ~NumberFont();
    NumberFont(const NumberFont&)=delete;
    NumberFont& operator=(const NumberFont&)=delete;
    static float measure(std::string_view text,NumberStyle style);
    float draw(Surface& surface,PixelRect clip,int x,int y,std::string_view text,NumberStyle style) const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
