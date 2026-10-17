#include "ui/surface.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstdint>
namespace ui {
Surface::Surface(int width,int height,std::span<unsigned char> pixels):width_(width),height_(height),clip_{0,0,width,height},pixels_(pixels) {
    if(width<1||height<1||static_cast<std::uint64_t>(width)*height*3!=pixels.size()) throw std::invalid_argument("invalid RGB surface");
}
void Surface::set_clip(PixelRect r) noexcept {
    const int left=std::clamp(r.x,0,width_),top=std::clamp(r.y,0,height_);
    const int right=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(r.x)+std::max(0,r.width),left,width_));
    const int bottom=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(r.y)+std::max(0,r.height),top,height_));
    clip_={left,top,right-left,bottom-top};
}
void Surface::clear(Color c) noexcept {
    const auto saved=clip_; clip_={0,0,width_,height_}; rectangle(clip_,c); clip_=saved;
}
void Surface::rectangle(PixelRect r,Color c) noexcept {
    if(r.width<=0||r.height<=0) return;
    const int left=std::clamp(r.x,clip_.x,clip_.right()),top=std::clamp(r.y,clip_.y,clip_.bottom());
    const int right=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(r.x)+r.width,clip_.x,clip_.right()));
    const int bottom=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(r.y)+r.height,clip_.y,clip_.bottom()));
    for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
        const auto p=(static_cast<std::size_t>(y)*width_+x)*3;
        pixels_[p]=c.r; pixels_[p+1]=c.g; pixels_[p+2]=c.b;
    }
}
void Surface::rounded_rectangle(PixelRect r,int radius,Color color) noexcept {
    if(r.width<=0||r.height<=0) return;
    radius=std::clamp(radius,0,std::min(r.width,r.height)/2);
    if(!radius) { rectangle(r,color); return; }
    rectangle({r.x+radius,r.y,r.width-2*radius,r.height},color);
    rectangle({r.x,r.y+radius,radius,r.height-2*radius},color);
    rectangle({r.right()-radius,r.y+radius,radius,r.height-2*radius},color);
    // Only the four small corner regions need coverage sampling, not the panel.
    for(int y=0;y<radius;++y) for(int x=0;x<radius;++x) {
        unsigned coverage=0;
        for(int sy=0;sy<4;++sy) for(int sx=0;sx<4;++sx) {
            const double dx=radius-(x+(sx+.5)/4),dy=radius-(y+(sy+.5)/4);
            coverage+=dx*dx+dy*dy<=static_cast<double>(radius)*radius;
        }
        const unsigned alpha=(coverage*255+8)/16;
        blend(r.x+x,r.y+y,color,alpha);
        blend(r.right()-1-x,r.y+y,color,alpha);
        blend(r.x+x,r.bottom()-1-y,color,alpha);
        blend(r.right()-1-x,r.bottom()-1-y,color,alpha);
    }
}
void Surface::blend(int x,int y,Color c,unsigned alpha) noexcept {
    if(x<clip_.x||y<clip_.y||x>=clip_.right()||y>=clip_.bottom()) return;
    alpha=std::min(alpha,255u);
    const auto p=(static_cast<std::size_t>(y)*width_+x)*3;
    const unsigned char channels[]={c.r,c.g,c.b};
    for(int k=0;k<3;++k) pixels_[p+k]=static_cast<unsigned char>((pixels_[p+k]*(255-alpha)+channels[k]*alpha+127)/255);
}
}
