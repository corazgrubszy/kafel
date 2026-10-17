#include "surface.hpp"

#include <algorithm>
#include <stdexcept>

namespace kafel {
namespace {
Rect intersect(Rect a, Rect b) noexcept {
    return {std::max(a.left, b.left), std::max(a.top, b.top),
            std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}
} // namespace

Surface::Surface(std::span<Pixel> pixels, int width, int height, std::size_t stride)
    : pixels_(pixels), width_(width), height_(height), stride_(stride),
      clip_{0, 0, width, height} {
    if (width <= 0 || height <= 0 || stride < static_cast<std::size_t>(width)) {
        throw std::invalid_argument("invalid surface dimensions or stride");
    }
    // Division avoids overflow from multiplying untrusted dimensions.
    // Require storage for complete rows, including the final row's padding.
    if (static_cast<std::size_t>(height) > pixels.size() / stride) {
        throw std::invalid_argument("surface buffer is too small");
    }
}

void Surface::set_clip(Rect rect) noexcept {
    clip_ = intersect(rect, {0, 0, width_, height_});
}

void Surface::reset_clip() noexcept { clip_ = {0, 0, width_, height_}; }

void Surface::clear(Pixel color) noexcept {
    const Rect saved = clip_;
    reset_clip();
    fill_rect(clip_, color);
    clip_ = saved;
}

void Surface::fill_rect(Rect rect, Pixel color) noexcept {
    const Rect visible = intersect(rect, clip_);
    if (visible.left >= visible.right || visible.top >= visible.bottom) return;
    for (int y = visible.top; y < visible.bottom; ++y) {
        Pixel* row = pixels_.data() + static_cast<std::size_t>(y) * stride_;
        std::fill(row + visible.left, row + visible.right, color);
    }
}
bool Surface::copy_rgb888(std::span<const std::uint8_t> source) noexcept {
    if(source.size()!=static_cast<std::size_t>(width_)*height_*3) return false;
    for(int y=0;y<height_;++y) for(int x=0;x<width_;++x) {
        const auto input=(static_cast<std::size_t>(y)*width_+x)*3;
        pixels_[static_cast<std::size_t>(y)*stride_+x]=rgb(source[input],source[input+1],source[input+2]);
    }
    return true;
}

} // namespace kafel
