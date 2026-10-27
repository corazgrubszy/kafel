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
    struct GlyphRun {
        std::string_view text;
        TextStyle style;
    };
    TextFont(std::span<const unsigned char> bytes, std::span<const GlyphRun> runs, std::size_t mask_limit);
    TextFont(const char *path, std::string_view repertoire, std::span<const TextStyle> styles);
    TextFont(std::span<const unsigned char> bytes, std::string_view repertoire,
             std::span<const TextStyle> styles, MissingGlyph policy = MissingGlyph::Reject,
             std::size_t mask_limit = ~std::size_t{});
    std::size_t prepared_bytes() const noexcept;
    std::size_t storage_bytes() const noexcept;
    int cap_height(TextStyle) const;
    PixelRect ink_bounds(std::string_view text, TextStyle style) const;
    ~TextFont();
    TextFont(const TextFont &) = delete;
    TextFont &operator=(const TextFont &) = delete;
    float measure(std::string_view text, TextStyle style) const;
    void draw(Surface &surface, PixelRect clip, int x, int top, std::string_view text, TextStyle style) const;

  private:
    void prepare(std::string_view repertoire, std::span<const TextStyle> styles);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Tabular numeric runs over the same trusted TrueType face used for hero text.
// Digits occupy equal cells, punctuation keeps its natural advance. No shaping engine.
class NumberFont {
  public:
    NumberFont(std::span<const unsigned char> bytes, std::span<const TextStyle> styles);
    ~NumberFont();
    NumberFont(const NumberFont &) = delete;
    NumberFont &operator=(const NumberFont &) = delete;
    float advance(char character, TextStyle) const;
    int height(TextStyle) const;
    int baseline(TextStyle) const;
    float measure(std::string_view, TextStyle) const;
    float draw(Surface &, PixelRect, int x, int top, std::string_view, TextStyle) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ui
