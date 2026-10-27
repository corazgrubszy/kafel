#include "ui/tile.hpp"
#include "ui/grid.hpp"
#include "ui/segmented_meter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace ui {
void TilePainter::panel(Surface &surface, PixelRect r, PanelStyle style) {
    surface.rounded_rectangle(r, style.radius, style.fill);
}
bool TilePainter::value(Surface &surface, PixelRect r, const ValueTile &model) const {
    const int x = r.x + 8;
    if (model.value.empty()) {
        numbers_.draw(surface, r, x, r.y + 29, "--", style_.number);
        return true;
    }
    auto style = style_.number;
    const float unit = model.unit.empty() ? 0 : 1 + text_.measure(model.unit, style_.unit);
    if (numbers_.measure(model.value, style) + unit > r.width - 16)
        style = style_.compact_number;
    if (numbers_.measure(model.value, style) + unit > r.width - 16) {
        text_.draw(surface, r, x, r.y + 29, "###", style_.label);
        return false;
    }
    const float end = numbers_.draw(surface, r, x, r.y + 29, model.value, style);
    text_.draw(surface, r, static_cast<int>(std::lround(end)) + 1,
               r.y + 29 + numbers_.baseline(style) - text_.cap_height(style_.unit), model.unit, style_.unit);
    return true;
}
void TilePainter::level(Surface &surface, PixelRect bounds, unsigned basis_points, unsigned alpha,
                        Color background) const {
    const auto geometry = SegmentedMeter::fit(bounds.x + 8, bounds.width - 16);
    const auto opacity = std::min(alpha, 255U);
    auto faded = [&](Color color) {
        auto mix = [&](unsigned fg, unsigned bg) {
            return static_cast<unsigned char>((fg * opacity + bg * (255 - opacity) + 127) / 255);
        };
        return Color{mix(color.r, background.r), mix(color.g, background.g), mix(color.b, background.b)};
    };
    const auto filled = faded(style_.meter.filled), empty = faded(style_.meter.empty);
    for (int i = 0; i < geometry.count; ++i)
        surface.rectangle(geometry.segment(i, bounds.bottom() - 9 - style_.meter.height, style_.meter.height),
                          i < geometry.filled(basis_points) ? filled : empty);
}
void TilePainter::metric(Surface &surface, PixelRect r, const MetricTile &model) const {
    if (r.width < 74 || r.height < 76 || (model.percent && *model.percent > 100))
        throw std::invalid_argument("invalid metric tile");
    panel(surface, r, style_.panel);
    const int inset = style_.panel.inset;
    // Balance actual glyph ink, not the font's invisible em box. Equal outer
    // padding frames three elements; the reading is centred in the space left
    // between the label and meter. Resource readings always use the same size.
    const auto label_ink = text_.ink_bounds(model.label, style_.label);
    const int label_top = r.y + inset;
    const int label_bottom = label_top + label_ink.height;
    const auto &meter = style_.meter;
    const int meter_top = r.bottom() - inset - meter.height;
    const auto style = style_.number;
    const int unit_top = numbers_.baseline(style) - text_.cap_height(style_.unit);
    const auto unit_ink = text_.ink_bounds("%", style_.unit);
    const int reading_height = std::max(numbers_.height(style), unit_top + unit_ink.y + unit_ink.height);
    const int number_top = label_bottom + (meter_top - label_bottom - reading_height) / 2;
    text_.draw(surface, r, r.x + inset, label_top - label_ink.y, model.label, style_.label);
    if (model.percent) {
        char value[4];
        // Only the displayed number saturates; the meter keeps the real measurement.
        std::snprintf(value, sizeof(value), "%u", std::min(*model.percent, 99u));
        const float right = numbers_.draw(surface, r, r.x + inset, number_top, value, style);
        text_.draw(surface, r, static_cast<int>(std::lround(right)) + 1,
                   number_top + numbers_.baseline(style) - text_.cap_height(style_.unit), "%", style_.unit);
    } else
        numbers_.draw(surface, r, r.x + inset, number_top, "--", style);
    Grid segments({r.x + inset, meter_top, r.width - 2 * inset, meter.height}, meter.segments, meter.gap);
    const int filled = filled_segments(model.percent.value_or(0), meter.segments);
    for (int i = 0; i < meter.segments; ++i)
        surface.rectangle(*segments.cell(i), i < filled ? meter.filled : meter.empty);
}
} // namespace ui
