#pragma once
#include "geometry.hpp"
namespace ui {
struct PanelStyle { Color fill; int inset=8; int radius=6; };
struct TextStyle { float em_px; Color color; float tracking=0; };
struct NumberStyle { float scale; Color color; };
struct MeterStyle { Color filled,empty; int segments=10; int gap=2; int height=6; };
struct StatusStyle { PanelStyle panel; Color accent; };
struct Theme {
    Color background;
    PanelStyle clock,metric;
    TextStyle eyebrow,hostname,date,label,unit;
    NumberStyle clock_number,metric_number,metric_number_compact;
    MeterStyle meter;
    StatusStyle warning,error,offline;
    TextStyle service_name,service_state,service_detail,attention;
};
// Selected roles from Rosé Pine Moon (https://github.com/rose-pine/palette).
// Palette copyright (c) mvllow; MIT notice: vendor/rose-pine-palette/LICENSE.
// No dependency on a renderer.
inline constexpr Theme rose_pine_moon{
    Color::hex(0x232136),
    {Color::hex(0x393552),10,6},
    {Color::hex(0x2a273f),8,6},
    {7.5f,Color::hex(0xe0def4),.3f},
    {14,Color::hex(0xe0def4),0},
    {14,Color::hex(0xe0def4),0},
    {14,Color::hex(0xe0def4),0},
    {14,Color::hex(0xe0def4),0},
    {1.12f,Color::hex(0xc4a7e7)},
    {.47f,Color::hex(0xe0def4)},
    {.35f,Color::hex(0xe0def4)},
    {Color::hex(0xf6c177),Color::hex(0x393552),10,2,10},
    {{Color::hex(0x393552),8,6},Color::hex(0xf6c177)},
    {{Color::hex(0x393552),8,6},Color::hex(0xeb6f92)},
    {{Color::hex(0x232136),8,6},Color::hex(0x908caa)},
    {14,Color::hex(0xe0def4),0},
    {14,Color::hex(0xc4a7e7),0},
    {18,Color::hex(0xe0def4),0},
    {12,Color::hex(0xf6c177),0}
};
}
