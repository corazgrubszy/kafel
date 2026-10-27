#pragma once
#include "geometry.hpp"
namespace ui {
struct PanelStyle {
    Color fill;
    int inset = 8;
    int radius = 6;
};
struct TextStyle {
    float em_px;
    Color color;
    float tracking = 0;
};
struct MeterStyle {
    Color filled, empty;
    int segments = 10;
    int gap = 2;
    int height = 6;
};
struct MetricStyle {
    PanelStyle panel;
    TextStyle label, number, compact_number, unit;
    MeterStyle meter;
};
} // namespace ui
