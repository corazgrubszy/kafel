#include "clock_painter.hpp"
#include "dashboard/large_tile_layout.hpp"
#include "ui/tile.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace kafel {

using ui::PixelRect;
using ui::Surface;
void ClockPainter::paint(ui::Surface &surface, PixelRect r, const ClockTile &model) const {
    if (r.width < 232 || r.height < 100)
        throw std::invalid_argument("clock tile too small");
    if (model.time.size() != 5 || model.time[2] != ':' || model.time[0] < '0' || model.time[0] > '2' ||
        model.time[1] < '0' || model.time[1] > '9' || (model.time[0] == '2' && model.time[1] > '3') ||
        model.time[3] < '0' || model.time[3] > '5' || model.time[4] < '0' || model.time[4] > '9')
        throw std::invalid_argument("clock expects HH:MM");
    ui::TilePainter::panel(surface, r, theme_.clock);
    const int inset = theme_.clock.inset;
    const int origin_y = r.y - LargeTileLayout::panel_inset;
    const int title_top = origin_y + LargeTileLayout::title_top;
    auto status_style = theme_.secondary;
    status_style.color = model.status_color;
    const int status_width = static_cast<int>(std::ceil(text_.measure(model.status, status_style)));
    const int status_x = r.right() - inset - status_width;
    const int heading_width = status_width ? status_x - 10 - (r.x + inset) : r.width - 2 * inset;
    const PixelRect hostname_clip{r.x + inset, title_top, heading_width, 26};
    const auto hostname_ink = headings_.ink_bounds(model.hostname, theme_.hostname);
    headings_.draw(surface, hostname_clip, hostname_clip.x, title_top - hostname_ink.y, model.hostname,
                   theme_.hostname);
    if (!model.subheading.empty())
        text_.draw(surface, {r.x + inset, origin_y + 44, r.width - 2 * inset, 20}, r.x + inset, origin_y + 46,
                   model.subheading, theme_.secondary);
    if (!model.status.empty())
        text_.draw(surface, r, status_x, origin_y + 18, model.status, status_style);
    const auto style = theme_.clock_number;
    const int height = numbers_.height(style);
    const int top = origin_y + LargeTileLayout::value_baseline - numbers_.baseline(style);
    if (model.previous_time.size() == 5 && model.clock_progress < 1) {
        float pen = static_cast<float>(r.x + inset);
        const int shift =
            static_cast<int>(std::lround(std::clamp(model.clock_progress, 0.0f, 1.0f) * height));
        for (std::size_t i = 0; i < 5; ++i) {
            const char c = model.time[i];
            const int advance = static_cast<int>(std::ceil(numbers_.advance(c, style)));
            const PixelRect cell{static_cast<int>(std::lround(pen)), top, advance, height};
            if (c == model.previous_time[i])
                numbers_.draw(surface, cell, cell.x, top, model.time.substr(i, 1), style);
            else {
                numbers_.draw(surface, cell, cell.x, top + shift, model.previous_time.substr(i, 1), style);
                numbers_.draw(surface, cell, cell.x, top - height + shift, model.time.substr(i, 1), style);
            }
            pen += numbers_.advance(c, style);
        }
    } else
        numbers_.draw(surface, r, r.x + inset, top, model.time, style);
    text_.draw(surface, r, r.x + inset,
               origin_y + LargeTileLayout::caption_baseline - text_.cap_height(theme_.date), model.date,
               theme_.date);
}
} // namespace kafel
