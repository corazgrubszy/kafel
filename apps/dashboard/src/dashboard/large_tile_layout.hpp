#pragma once
namespace kafel {
// Absolute coordinates inside a 240x240 page; baseline positions use real font metrics.
struct LargeTileLayout {
    static constexpr int panel_inset = 4, inset = 14, title_top = 14;
    static constexpr int illustration_top = 52, illustration_height = 64;
    static constexpr int value_baseline = 192, caption_baseline = 223;
    static constexpr float title_size = 22, value_size = 60, caption_size = 18;
    static constexpr float failure_value_size = 60;
};
} // namespace kafel
