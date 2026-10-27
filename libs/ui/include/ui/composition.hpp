#pragma once
#include "font.hpp"
#include <array>
#include <span>
#include <string_view>
#include <variant>
namespace ui {
// All spans and strings are borrowed; definitions and fonts outlive the painter.
struct Point {
    float x, y;
};
struct Backdrop {
    Color base;
    Point glow_center, glow_radius;
    std::array<float, 3> glow_gain;
    int stripe_start, stripe_period, stripe_width;
    Color stripe_gain;
    int corner_radius;
};
enum class ValueFormat { Text, Integer, General };
struct Field {
    std::string_view name;
    std::variant<double, std::string_view> value;
};
struct FieldRule {
    std::string_view name;
    ValueFormat format;
    double minimum = 0, maximum = 0;
    std::size_t maximum_bytes = 0;
};
struct Content {
    std::string_view literal{}, field{}, prefix{}, suffix{};
    ValueFormat format = ValueFormat::Text;
};
enum class Overflow { Clip, Ellipsis, Marquee };
struct TextElement {
    int x, y;
    PixelRect clip;
    unsigned font;
    TextStyle style;
    Content content;
    Overflow overflow = Overflow::Clip;
};
struct MetricElement {
    int x, y;
    PixelRect clip;
    unsigned font;
    std::span<const TextStyle> sizes;
    float fit_width;
    Content content;
    std::string_view unit;
    TextStyle unit_style;
    int unit_y, unit_gap;
};
struct RgbaImage {
    int width, height;
    std::span<const unsigned char> pixels;
};
struct Composition {
    int width, height;
    Backdrop backdrop;
    std::span<const TextElement> texts;
    std::span<const MetricElement> metrics;
    std::span<const FieldRule> fields;
};
// Validate geometry, references and limits without constructing font resources.
void validate_composition(const Composition &, std::size_t font_count);
class CompositionPainter {
  public:
    CompositionPainter(const Composition &composition, std::span<const TextFont *const> fonts);
    CompositionPainter(Composition &&, std::span<const TextFont *const>) = delete;
    // Validate all bindings and glyphs before touching the destination.
    // Numeric formatting uses fixed stack buffers. No scripts or domain knowledge.
    void paint(Surface &destination, std::span<const Field> values, const RgbaImage *image = nullptr) const;

  private:
    const Composition &composition_;
    std::span<const TextFont *const> fonts_;
};
} // namespace ui
