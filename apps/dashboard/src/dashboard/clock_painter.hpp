#pragma once
#include "theme.hpp"
#include "ui/font.hpp"
namespace kafel {
struct ClockTile {
    std::string_view hostname, time, date; // Borrowed during draw only.
    std::string_view previous_time{};
    float clock_progress = 1;
    std::string_view subheading{};
    std::string_view status{};
    ui::Color status_color = ui::Color::hex(0xaaa4c5);
};

// Product-specific clock layout. Generic UI only supplies text and drawing.
class ClockPainter {
  public:
    ClockPainter(const DashboardTheme &theme, const ui::TextFont &text, const ui::TextFont &headings,
                 const ui::NumberFont &numbers)
        : theme_(theme), text_(text), headings_(headings), numbers_(numbers) {}
    void paint(ui::Surface &, ui::PixelRect, const ClockTile &) const;

  private:
    const DashboardTheme &theme_;
    const ui::TextFont &text_, &headings_;
    const ui::NumberFont &numbers_;
};
} // namespace kafel
