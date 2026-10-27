#pragma once
#include "font.hpp"
#include <optional>
#include <string_view>
namespace ui {
struct MetricTile {
    std::string_view label;
    std::optional<unsigned> percent;
};
struct ValueTile {
    std::string_view value, unit;
};
// Stateless tile presentation: all colours and typography come from MetricStyle.
// No service knowledge, framebuffer access, gestures or animation state.
class TilePainter {
  public:
    TilePainter(MetricStyle style, const TextFont &text, const NumberFont &numbers)
        : style_(style), text_(text), numbers_(numbers) {}
    // Main section only. False means explicit ### overflow marker was drawn.
    bool value(Surface &surface, PixelRect bounds, const ValueTile &model) const;
    void metric(Surface &surface, PixelRect bounds, const MetricTile &model) const;
    // Fixed-width segments and gaps; unused pixels stay at the two ends.
    void level(Surface &, PixelRect bounds, unsigned basis_points, unsigned alpha, Color background) const;
    static void panel(Surface &surface, PixelRect bounds, PanelStyle style);
    static constexpr int filled_segments(unsigned percent, int count) noexcept {
        if (count <= 0)
            return 0;
        const auto bounded = percent > 100 ? 100 : percent;
        return static_cast<int>((static_cast<std::uint64_t>(bounded) * count + 99) / 100);
    }

  private:
    MetricStyle style_;
    const TextFont &text_;
    const NumberFont &numbers_;
};
} // namespace ui
