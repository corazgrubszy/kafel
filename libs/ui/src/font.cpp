#include "ui/font.hpp"
#include "stb/stb_truetype.h"
#include "text/utf8.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace ui {
namespace {
constexpr int samples = 4;
char32_t next(std::string_view &text) {
    const auto decoded = kafel::utf8::decode(text);
    if (!decoded.valid)
        throw std::invalid_argument("invalid UTF-8");
    return decoded.codepoint;
}
struct Mask {
    int width, height;
    std::vector<unsigned char> pixels;
};
void draw_mask(Surface &surface, PixelRect clip, int left, int top, const Mask &mask, Color color) {
    const int x0 = static_cast<int>(std::floor(left / static_cast<float>(samples)));
    const int y0 = static_cast<int>(std::floor(top / static_cast<float>(samples)));
    const int x1 = static_cast<int>(std::ceil((left + mask.width) / static_cast<float>(samples)));
    const int y1 = static_cast<int>(std::ceil((top + mask.height) / static_cast<float>(samples)));
    for (int y = std::max(y0, clip.y); y < std::min(y1, clip.bottom()); ++y)
        for (int x = std::max(x0, clip.x); x < std::min(x1, clip.right()); ++x) {
            unsigned coverage = 0;
            for (int sy = 0; sy < samples; ++sy)
                for (int sx = 0; sx < samples; ++sx) {
                    const int mx = x * samples + sx - left, my = y * samples + sy - top;
                    if (mx >= 0 && my >= 0 && mx < mask.width && my < mask.height)
                        coverage += mask.pixels[my * mask.width + mx];
                }
            surface.blend(x, y, color, (coverage + 8) / 16);
        }
}
} // namespace
struct TextFont::Impl {
    struct Glyph {
        char32_t cp;
        float size, advance;
        int index, x, y, cap_top;
        Mask mask;
    };
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    std::vector<Glyph> glyphs;
    MissingGlyph policy = MissingGlyph::Reject;
    std::size_t mask_bytes = 0, mask_limit = ~std::size_t{};
    const Glyph &glyph(char32_t cp, float size) const {
        for (const auto &g : glyphs)
            if (g.cp == cp && g.size == size)
                return g;
        if (policy == MissingGlyph::QuestionMark && cp != U'?')
            return glyph(U'?', size);
        throw std::invalid_argument("text glyph or size not prepared");
    }
};
TextFont::TextFont(const char *path, std::string_view repertoire, std::span<const TextStyle> styles)
    : impl_(std::make_unique<Impl>()) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (!file || length <= 0 || length > 4 * 1024 * 1024)
        throw std::invalid_argument("cannot read trusted font");
    impl_->bytes.resize(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(impl_->bytes.data()), length) ||
        !stbtt_InitFont(&impl_->info, impl_->bytes.data(), 0))
        throw std::invalid_argument("invalid trusted font");
    prepare(repertoire, styles);
}
TextFont::TextFont(std::span<const unsigned char> bytes, std::string_view repertoire,
                   std::span<const TextStyle> styles, MissingGlyph policy, std::size_t mask_limit)
    : impl_(std::make_unique<Impl>()) {
    if (bytes.empty() || bytes.size() > 4 * 1024 * 1024)
        throw std::invalid_argument("invalid trusted font size");
    impl_->bytes.assign(bytes.begin(), bytes.end());
    impl_->policy = policy;
    impl_->mask_limit = mask_limit;
    if (!stbtt_InitFont(&impl_->info, impl_->bytes.data(), 0))
        throw std::invalid_argument("invalid trusted font");
    prepare(repertoire, styles);
    if (policy == MissingGlyph::QuestionMark)
        for (auto style : styles)
            (void)impl_->glyph(U'?', style.em_px);
}
TextFont::TextFont(std::span<const unsigned char> bytes, std::span<const GlyphRun> runs, std::size_t limit)
    : TextFont(bytes, std::string_view{}, std::span<const TextStyle>{}, MissingGlyph::Reject, limit) {
    for (const auto &run : runs)
        prepare(run.text, std::span(&run.style, 1));
}
void TextFont::prepare(std::string_view repertoire, std::span<const TextStyle> styles) {
    for (const auto style : styles) {
        if (!std::isfinite(style.em_px) || style.em_px < 1 || style.em_px > 128)
            throw std::invalid_argument("invalid font size");
        auto remaining = repertoire;
        while (!remaining.empty()) {
            const auto cp = next(remaining);
            if (std::any_of(impl_->glyphs.begin(), impl_->glyphs.end(),
                            [&](const auto &g) { return g.cp == cp && g.size == style.em_px; }))
                continue;
            const int index = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
            if (!index)
                throw std::invalid_argument("missing trusted font glyph");
            const float scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, style.em_px * samples);
            int x, y, right, bottom, advance, cap;
            stbtt_GetGlyphBitmapBox(&impl_->info, index, scale, scale, &x, &y, &right, &bottom);
            stbtt_GetCodepointBitmapBox(&impl_->info, 'H', scale, scale, nullptr, &cap, nullptr, nullptr);
            stbtt_GetGlyphHMetrics(&impl_->info, index, &advance, nullptr);
            Impl::Glyph g{cp, style.em_px, advance * scale / samples,  index, x,
                          y,  cap,         {right - x, bottom - y, {}}};
            const auto bytes = static_cast<std::size_t>(g.mask.width * g.mask.height);
            if (bytes > impl_->mask_limit - impl_->mask_bytes)
                throw std::invalid_argument("limit_exceeded");
            impl_->mask_bytes += bytes;
            g.mask.pixels.resize(bytes);
            if (!g.mask.pixels.empty())
                stbtt_MakeGlyphBitmap(&impl_->info, g.mask.pixels.data(), g.mask.width, g.mask.height,
                                      g.mask.width, scale, scale, index);
            impl_->glyphs.push_back(std::move(g));
        }
    }
}
TextFont::~TextFont() = default;
std::size_t TextFont::prepared_bytes() const noexcept { return impl_->mask_bytes; }
std::size_t TextFont::storage_bytes() const noexcept {
    std::size_t n = impl_->bytes.capacity() + impl_->glyphs.capacity() * sizeof(Impl::Glyph);
    for (const auto &g : impl_->glyphs)
        n += g.mask.pixels.capacity();
    return n;
}
float TextFont::measure(std::string_view text, TextStyle style) const {
    float width = 0;
    int previous = 0;
    const float scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, style.em_px);
    while (!text.empty()) {
        const auto &g = impl_->glyph(next(text), style.em_px);
        if (previous)
            width += stbtt_GetGlyphKernAdvance(&impl_->info, previous, g.index) * scale + style.tracking;
        width += g.advance;
        previous = g.index;
    }
    return width;
}
void TextFont::draw(Surface &surface, PixelRect clip, int x, int top, std::string_view text,
                    TextStyle style) const {
    float pen = static_cast<float>(x);
    int previous = 0;
    const float scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, style.em_px);
    while (!text.empty()) {
        const auto &g = impl_->glyph(next(text), style.em_px);
        if (previous)
            pen += stbtt_GetGlyphKernAdvance(&impl_->info, previous, g.index) * scale + style.tracking;
        draw_mask(surface, clip, static_cast<int>(std::lround(pen * samples)) + g.x,
                  top * samples - g.cap_top + g.y, g.mask, style.color);
        pen += g.advance;
        previous = g.index;
    }
}
int TextFont::cap_height(TextStyle style) const {
    for (const auto &glyph : impl_->glyphs)
        if (glyph.size == style.em_px)
            return static_cast<int>(std::ceil(-glyph.cap_top / static_cast<float>(samples)));
    throw std::invalid_argument("font size not prepared");
}
PixelRect TextFont::ink_bounds(std::string_view text, TextStyle style) const {
    float left = 0, right = 0, top = 0, bottom = 0, pen = 0;
    bool found = false;
    int previous = 0;
    const float scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, style.em_px);
    while (!text.empty()) {
        const auto &g = impl_->glyph(next(text), style.em_px);
        if (previous)
            pen += stbtt_GetGlyphKernAdvance(&impl_->info, previous, g.index) * scale + style.tracking;
        if (g.mask.width && g.mask.height) {
            const float x = pen + g.x / static_cast<float>(samples),
                        y = (g.y - g.cap_top) / static_cast<float>(samples);
            const float end_x = x + g.mask.width / static_cast<float>(samples),
                        end_y = y + g.mask.height / static_cast<float>(samples);
            if (!found) {
                left = x;
                right = end_x;
                top = y;
                bottom = end_y;
                found = true;
            } else {
                left = std::min(left, x);
                right = std::max(right, end_x);
                top = std::min(top, y);
                bottom = std::max(bottom, end_y);
            }
        }
        pen += g.advance;
        previous = g.index;
    }
    if (!found)
        return {0, 0, 0, 0};
    const int x = static_cast<int>(std::floor(left)), y = static_cast<int>(std::floor(top));
    return {x, y, static_cast<int>(std::ceil(right)) - x, static_cast<int>(std::ceil(bottom)) - y};
}
struct NumberFont::Impl {
    TextFont font;
    struct Metrics {
        float size, digit, colon, minus, point;
        int height, top, baseline;
    };
    std::vector<Metrics> metrics;
    Impl(std::span<const unsigned char> bytes, std::span<const TextStyle> styles)
        : font(bytes, "0123456789:-.", styles) {
        for (auto style : styles) {
            if (std::any_of(metrics.begin(), metrics.end(), [&](auto m) { return m.size == style.em_px; }))
                continue;
            float digit = 0;
            for (char c = '0'; c <= '9'; ++c)
                digit = std::max(digit, font.measure(std::string_view(&c, 1), style));
            const auto bounds = font.ink_bounds("0123456789:-.", style);
            metrics.push_back({style.em_px, digit, font.measure(":", style), font.measure("-", style),
                               font.measure(".", style), bounds.height, bounds.y,
                               font.cap_height(style) - bounds.y});
        }
    }
    const Metrics &at(TextStyle style) const {
        for (const auto &metric : metrics)
            if (metric.size == style.em_px)
                return metric;
        throw std::invalid_argument("number size not prepared");
    }
};
NumberFont::NumberFont(std::span<const unsigned char> bytes, std::span<const TextStyle> styles)
    : impl_(std::make_unique<Impl>(bytes, styles)) {}
NumberFont::~NumberFont() = default;
float NumberFont::advance(char c, TextStyle style) const {
    const auto &m = impl_->at(style);
    if (c >= '0' && c <= '9')
        return m.digit;
    if (c == ':')
        return m.colon;
    if (c == '-')
        return m.minus;
    if (c == '.')
        return m.point;
    throw std::invalid_argument("unsupported number character");
}
int NumberFont::height(TextStyle style) const { return impl_->at(style).height; }
int NumberFont::baseline(TextStyle style) const { return impl_->at(style).baseline; }
float NumberFont::measure(std::string_view value, TextStyle style) const {
    float width = 0;
    for (char c : value)
        width += advance(c, style);
    return width;
}
float NumberFont::draw(Surface &surface, PixelRect clip, int x, int top, std::string_view value,
                       TextStyle style) const {
    // Validate the complete run before touching the destination.
    (void)measure(value, style);
    float pen = static_cast<float>(x);
    for (char c : value) {
        const auto glyph = std::string_view(&c, 1);
        const auto cell = advance(c, style);
        const auto inset = (cell - impl_->font.measure(glyph, style)) / 2;
        impl_->font.draw(surface, clip, static_cast<int>(std::lround(pen + inset)),
                         top - impl_->at(style).top, glyph, style);
        pen += cell;
    }
    return pen;
}
} // namespace ui
