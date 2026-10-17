#pragma once
#include "font.hpp"
#include <string_view>
#include <optional>
namespace ui {
struct ClockTile {
    std::string_view label,hostname,time,date; // Borrowed during draw only.
    std::string_view previous_time{};
    float clock_progress=1;
    bool attention=false;
    std::string_view subheading{};
};
struct MetricTile {
    std::string_view label;
    std::optional<unsigned> percent;
};
struct ValueTile { std::string_view value,unit; };
// Stateless tile presentation: all colours and typography come from Theme.
// No service knowledge, framebuffer access, gestures or animation state.
class TilePainter {
public:
    TilePainter(const Theme& theme,const TextFont& text,const NumberFont& numbers):theme_(theme),text_(text),numbers_(numbers) {}
    void clock(Surface& surface,PixelRect bounds,const ClockTile& model) const;
    // Main section only. False means explicit ### overflow marker was drawn.
    bool value(Surface& surface,PixelRect bounds,const ValueTile& model) const;
    void metric(Surface& surface,PixelRect bounds,const MetricTile& model) const;
    // Fixed-width segments and gaps; unused pixels stay at the two ends.
    void level(Surface&,PixelRect bounds,unsigned basis_points,unsigned alpha,Color background) const;
    static void panel(Surface& surface,PixelRect bounds,PanelStyle style);
    static constexpr int filled_segments(unsigned percent,int count) noexcept {
        if(count<=0) return 0;
        const auto bounded=percent>100?100:percent;
        return static_cast<int>((static_cast<std::uint64_t>(bounded)*count+99)/100);
    }
private:
    const Theme& theme_; const TextFont& text_; const NumberFont& numbers_;
};
}
