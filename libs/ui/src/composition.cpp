#include "ui/composition.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace ui {
namespace {
constexpr std::size_t maximum_elements = 64, maximum_fields = 32, content_capacity = 128;
[[noreturn]] void invalid() { throw std::invalid_argument("invalid composition or bindings"); }
const Field &field(std::span<const Field> fields, std::string_view name) {
    for (const auto &value : fields)
        if (value.name == name)
            return value;
    invalid();
}
const FieldRule &rule(std::span<const FieldRule> rules, std::string_view name) {
    for (const auto &value : rules)
        if (value.name == name)
            return value;
    invalid();
}
struct Formatted {
    std::array<char, content_capacity> bytes{};
    std::size_t length = 0;
    void append(std::string_view value) {
        if (value.size() > bytes.size() - length)
            invalid();
        if (!value.empty())
            std::memcpy(bytes.data() + length, value.data(), value.size());
        length += value.size();
    }
    std::string_view view() const { return {bytes.data(), length}; }
};
Formatted resolve(const Content &content, std::span<const Field> values) {
    Formatted output;
    output.append(content.prefix);
    if (content.field.empty())
        output.append(content.literal);
    else {
        const auto &value = field(values, content.field).value;
        if (content.format == ValueFormat::Text)
            output.append(std::get<std::string_view>(value));
        else {
            char number[64];
            auto scalar = std::get<double>(value);
            scalar =
                content.format == ValueFormat::Integer ? std::round(scalar) : std::round(scalar * 10) / 10;
            const auto formatted =
                std::to_chars(number, number + sizeof number, scalar == 0 ? 0 : scalar,
                              std::chars_format::fixed, content.format == ValueFormat::Integer ? 0 : 1);
            if (formatted.ec != std::errc{})
                invalid();
            auto length = static_cast<std::size_t>(formatted.ptr - number);
            if (content.format == ValueFormat::General && length >= 2 && number[length - 2] == '.' &&
                number[length - 1] == '0')
                length -= 2;
            output.append({number, length});
        }
    }
    output.append(content.suffix);
    return output;
}
TextStyle metric_style(const TextFont &font, const MetricElement &metric, std::string_view text) {
    auto chosen = metric.sizes.back();
    for (auto size : metric.sizes) {
        chosen = size;
        if (font.measure(text, size) <= metric.fit_width)
            break;
    }
    // Never silently clip the numeric value if no prepared size fits the slot.
    if (font.measure(text, chosen) > metric.clip.width)
        invalid();
    return chosen;
}
void backdrop(Surface &surface, const Backdrop &b) {
    const int width = surface.width(), height = surface.height(), r = b.corner_radius;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float dx = (x - b.glow_center.x) / b.glow_radius.x,
                        dy = (y - b.glow_center.y) / b.glow_radius.y;
            const float glow = std::exp(-(dx * dx + dy * dy) * .5f);
            auto channel = [&](unsigned base, float gain) {
                return static_cast<unsigned char>(std::clamp(base + glow * gain, 0.f, 255.f));
            };
            Color color{channel(b.base.r, b.glow_gain[0]), channel(b.base.g, b.glow_gain[1]),
                        channel(b.base.b, b.glow_gain[2])};
            if (x > b.stripe_start && (x + y) % b.stripe_period < b.stripe_width) {
                color.r = static_cast<unsigned char>(std::min(255u, unsigned(color.r) + b.stripe_gain.r));
                color.g = static_cast<unsigned char>(std::min(255u, unsigned(color.g) + b.stripe_gain.g));
                color.b = static_cast<unsigned char>(std::min(255u, unsigned(color.b) + b.stripe_gain.b));
            }
            const int cx = x < r            ? r
                           : x >= width - r ? width - r - 1
                                            : x,
                      cy = y < r             ? r
                           : y >= height - r ? height - r - 1
                                             : y;
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r)
                color = {0, 0, 0};
            surface.blend(x, y, color, 255);
        }
}
} // namespace
void validate_composition(const Composition &c, std::size_t font_count) {
    if (c.width < 1 || c.width > 2048 || c.height < 1 || c.height > 2048 ||
        c.fields.size() > maximum_fields || c.texts.size() + c.metrics.size() > maximum_elements ||
        font_count == 0 || font_count > 8)
        invalid();
    const auto &b = c.backdrop;
    if (!std::isfinite(b.glow_center.x) || !std::isfinite(b.glow_center.y) ||
        !std::isfinite(b.glow_radius.x) || !std::isfinite(b.glow_radius.y) || b.glow_radius.x <= 0 ||
        b.glow_radius.y <= 0 || b.stripe_start < 0 || b.stripe_period < 1 || b.stripe_width < 0 ||
        b.stripe_width > b.stripe_period || b.corner_radius < 0 ||
        b.corner_radius > std::min(c.width, c.height) / 2)
        invalid();
    for (float gain : b.glow_gain)
        if (!std::isfinite(gain) || gain < 0 || gain > 255)
            invalid();
    auto bounds = [&](PixelRect r) {
        if (r.x < 0 || r.y < 0 || r.width < 1 || r.height < 1 || std::int64_t(r.x) + r.width > c.width ||
            std::int64_t(r.y) + r.height > c.height)
            invalid();
    };
    for (std::size_t i = 0; i < c.fields.size(); ++i) {
        const auto &f = c.fields[i];
        if (f.name.empty() || f.name.size() > 32)
            invalid();
        for (std::size_t j = 0; j < i; ++j)
            if (c.fields[j].name == f.name)
                invalid();
        if (f.format == ValueFormat::Text) {
            if (f.maximum_bytes < 1 || f.maximum_bytes > content_capacity)
                invalid();
        } else if (!std::isfinite(f.minimum) || !std::isfinite(f.maximum) || f.minimum > f.maximum)
            invalid();
    }
    auto style = [](TextStyle value) {
        if (!std::isfinite(value.em_px) || value.em_px < 1 || value.em_px > 128 ||
            !std::isfinite(value.tracking) || std::abs(value.tracking) > 128)
            invalid();
    };
    auto source = [&](const Content &value) {
        if (value.field.empty()) {
            if (value.format != ValueFormat::Text)
                invalid();
        } else {
            if (!value.literal.empty() || rule(c.fields, value.field).format != value.format)
                invalid();
        }
        if (value.prefix.size() + value.suffix.size() + value.literal.size() > content_capacity)
            invalid();
    };
    for (const auto &element : c.texts) {
        bounds(element.clip);
        source(element.content);
        style(element.style);
        if (element.font >= font_count || element.x < 0 || element.y < 0 || element.x >= c.width ||
            element.y >= c.height)
            invalid();
    }
    for (const auto &element : c.metrics) {
        bounds(element.clip);
        source(element.content);
        if (element.sizes.empty() || element.sizes.size() > 8)
            invalid();
        style(element.unit_style);
        for (auto size : element.sizes)
            style(size);
        if (element.font >= font_count || !std::isfinite(element.fit_width) || element.fit_width <= 0 ||
            element.x < 0 || element.y < 0 || element.x >= c.width || element.y >= c.height ||
            element.unit_y < 0 || element.unit_y >= c.height || element.unit_gap < 0 ||
            element.unit_gap > c.width || element.unit.size() > 32)
            invalid();
    }
}
CompositionPainter::CompositionPainter(const Composition &c, std::span<const TextFont *const> fonts)
    : composition_(c), fonts_(fonts) {
    validate_composition(c, fonts.size());
    for (auto font : fonts)
        if (!font)
            invalid();
}

void CompositionPainter::paint(Surface &surface, std::span<const Field> values,
                               const RgbaImage *image) const {
    const auto &c = composition_;
    if (surface.width() != c.width || surface.height() != c.height || values.size() != c.fields.size())
        invalid();
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j)
            if (values[j].name == values[i].name)
                invalid();
        const auto &constraint = rule(c.fields, values[i].name);
        if (constraint.format == ValueFormat::Text) {
            const auto *text = std::get_if<std::string_view>(&values[i].value);
            if (!text || text->size() > constraint.maximum_bytes)
                invalid();
        } else {
            const auto *value = std::get_if<double>(&values[i].value);
            if (!value || !std::isfinite(*value) || *value < constraint.minimum ||
                *value > constraint.maximum ||
                (constraint.format == ValueFormat::Integer && std::floor(*value) != *value))
                invalid();
        }
    }
    if (image && (image->width != c.width || image->height != c.height ||
                  image->pixels.size() != static_cast<std::size_t>(c.width) * c.height * 4))
        invalid();
    // Resolve and measure every element first: bad bindings, overflow and missing
    // prepared glyphs leave the previous frame untouched.
    for (const auto &element : c.texts) {
        auto text = resolve(element.content, values);
        (void)fonts_[element.font]->measure(text.view(), element.style);
    }
    for (const auto &element : c.metrics) {
        auto text = resolve(element.content, values);
        const auto &font = *fonts_[element.font];
        const auto style = metric_style(font, element, text.view());
        const auto unit_x =
            element.x + static_cast<int>(std::ceil(font.measure(text.view(), style))) + element.unit_gap;
        if (unit_x + font.measure(element.unit, element.unit_style) > c.width)
            invalid();
    }
    backdrop(surface, c.backdrop);
    if (image)
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x) {
                const auto offset = (static_cast<std::size_t>(y) * c.width + x) * 4;
                const auto rgba = image->pixels.subspan(offset, 4);
                surface.blend(x, y, {rgba[0], rgba[1], rgba[2]}, rgba[3]);
            }
    for (const auto &element : c.texts) {
        auto text = resolve(element.content, values);
        fonts_[element.font]->draw(surface, element.clip, element.x, element.y, text.view(), element.style);
    }
    const PixelRect frame{0, 0, c.width, c.height};
    for (const auto &element : c.metrics) {
        auto text = resolve(element.content, values);
        const auto &font = *fonts_[element.font];
        const auto style = metric_style(font, element, text.view());
        font.draw(surface, element.clip, element.x, element.y, text.view(), style);
        const int unit_x =
            element.x + static_cast<int>(std::ceil(font.measure(text.view(), style))) + element.unit_gap;
        font.draw(surface, frame, unit_x, element.unit_y, element.unit, element.unit_style);
    }
}
} // namespace ui
