#pragma once
#include "geometry.hpp"
#include <span>
namespace ui {
// Non-owning, tightly packed RGB8 destination. The caller owns its storage.
class Surface {
public:
    Surface(int width,int height,std::span<unsigned char> pixels);
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    void clear(Color color) noexcept;
    PixelRect clip() const noexcept { return clip_; }
    void set_clip(PixelRect bounds) noexcept;
    void rectangle(PixelRect rect,Color color) noexcept;
    void rounded_rectangle(PixelRect rect,int radius,Color color) noexcept;
    void blend(int x,int y,Color color,unsigned coverage) noexcept;
private:
    int width_,height_;
    PixelRect clip_;
    std::span<unsigned char> pixels_;
};
}
