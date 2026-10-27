#pragma once
#include "large_tile_layout.hpp"
#include "ui/theme.hpp"
namespace kafel {
using ui::Color;
using ui::MeterStyle;
using ui::PanelStyle;
using ui::TextStyle;
struct StatusStyle {
    PanelStyle panel;
    Color accent;
};
struct DashboardTheme {
    Color background;
    PanelStyle clock, metric;
    TextStyle eyebrow, hostname, date, label, unit;
    TextStyle clock_number, metric_number, metric_number_compact;
    MeterStyle meter;
    StatusStyle warning, error, offline;
    TextStyle service_name, service_state, service_detail, attention;
    TextStyle secondary{14, Color::hex(0xaaa4c5), 0};
    ui::MetricStyle metric_style() const noexcept {
        return {.panel = metric,
                .label = label,
                .number = metric_number,
                .compact_number = metric_number_compact,
                .unit = unit,
                .meter = meter};
    }
};
// Selected roles from Rosé Pine Moon (https://github.com/rose-pine/palette).
// Palette copyright (c) mvllow; MIT notice: vendor/rose-pine-palette/LICENSE.
// No dependency on a renderer.
inline constexpr DashboardTheme rose_pine_moon{Color::hex(0x232136),
                                               {Color::hex(0x393552), 10, 6},
                                               {Color::hex(0x2a273f), 8, 6},
                                               {7.5f, Color::hex(0xe0def4), .3f},
                                               {22, Color::hex(0xe0def4), 0},
                                               {18, Color::hex(0xe0def4), 0},
                                               {14, Color::hex(0xe0def4), 0},
                                               {14, Color::hex(0xe0def4), 0},
                                               {LargeTileLayout::value_size, Color::hex(0xc4a7e7), 0},
                                               {32, Color::hex(0xe0def4), 0},
                                               {24, Color::hex(0xe0def4), 0},
                                               {Color::hex(0xf6c177), Color::hex(0x393552), 10, 2, 10},
                                               {{Color::hex(0x393552), 8, 6}, Color::hex(0xf6c177)},
                                               {{Color::hex(0x393552), 8, 6}, Color::hex(0xeb6f92)},
                                               {{Color::hex(0x232136), 8, 6}, Color::hex(0x908caa)},
                                               {14, Color::hex(0xe0def4), 0},
                                               {14, Color::hex(0xc4a7e7), 0},
                                               {18, Color::hex(0xe0def4), 0},
                                               {12, Color::hex(0xf6c177), 0}};
} // namespace kafel
