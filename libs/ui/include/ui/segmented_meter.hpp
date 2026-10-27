#pragma once
#include "geometry.hpp"
#include <algorithm>
#include <cstdint>
namespace ui {
// Pixel-exact geometry. Never stretches segments or individual gaps.
struct SegmentedMeter {
    static constexpr int segment_width = 4, gap = 2;
    int left = 0, count = 0;
    static constexpr SegmentedMeter fit(int left, int width) noexcept {
        if (width < segment_width)
            return {left, 0};
        const int count = (width + gap) / (segment_width + gap);
        const int occupied = count * segment_width + (count - 1) * gap;
        return {left + (width - occupied) / 2, count};
    }
    constexpr PixelRect segment(int index, int top, int height) const noexcept {
        return {left + index * (segment_width + gap), top, segment_width, height};
    }
    constexpr int filled(unsigned basis_points) const noexcept {
        return static_cast<int>((static_cast<std::uint64_t>(std::min(basis_points, 10000U)) * count + 5000) /
                                10000);
    }
};
} // namespace ui
