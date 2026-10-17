#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace kafel {

// Numeric RGB565 value; byte order is the responsibility of the device backend.
using Pixel = std::uint16_t;
constexpr Pixel rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return static_cast<Pixel>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Half-open bounds: left/top included, right/bottom excluded.
// Reversed bounds represent an empty rectangle.
struct Rect { int left; int top; int right; int bottom; };

// Non-owning view. The caller owns the pixels and must keep them alive.
// Stride is measured in pixels, not bytes; padding is never modified.
class Surface {
public:
    Surface(std::span<Pixel> pixels, int width, int height, std::size_t stride);
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    Rect clip() const noexcept { return clip_; }
    void set_clip(Rect rect) noexcept; // Always intersected with screen bounds.
    void reset_clip() noexcept;
    void clear(Pixel color) noexcept; // Clears visible pixels, ignoring clip.
    void fill_rect(Rect rect, Pixel color) noexcept;
    bool copy_rgb888(std::span<const std::uint8_t> source) noexcept;

private:
    std::span<Pixel> pixels_;
    int width_;
    int height_;
    std::size_t stride_;
    Rect clip_;
};
} // namespace kafel
