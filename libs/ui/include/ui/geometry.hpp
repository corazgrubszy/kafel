#pragma once
#include <cstdint>
namespace ui {
struct Color {
    unsigned char r,g,b;
    constexpr bool operator==(const Color&) const = default;
    static constexpr Color hex(std::uint32_t rgb) noexcept {
        return {static_cast<unsigned char>(rgb>>16),static_cast<unsigned char>(rgb>>8),static_cast<unsigned char>(rgb)};
    }
};
struct PixelRect {
    int x,y,width,height;
    constexpr int right() const noexcept { return x+width; }
    constexpr int bottom() const noexcept { return y+height; }
    constexpr bool operator==(const PixelRect&) const = default;
};
}
