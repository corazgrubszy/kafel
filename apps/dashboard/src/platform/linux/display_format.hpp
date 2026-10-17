#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
namespace kafel::linux_device {
struct DisplayFormat {
    std::string path,driver;
    unsigned width=0,height=0,bpp=0,type=0,visual=0,stride=0,xoffset=0,yoffset=0,nonstd=0,grayscale=0;
    unsigned red_offset=0,red_length=0,red_msb=0,green_offset=0,green_length=0,green_msb=0,blue_offset=0,blue_length=0,blue_msb=0,alpha_length=0;
    std::uint64_t memory=0;
    bool supported() const noexcept {
        return type==0&&visual==2&&width==240&&height==320&&bpp==16&&xoffset==0&&yoffset==0&&
            (!nonstd||(nonstd==1&&driver=="fb_st7789v"))&&!grayscale&&
            red_offset==11&&red_length==5&&!red_msb&&green_offset==5&&green_length==6&&!green_msb&&
            blue_offset==0&&blue_length==5&&!blue_msb&&!alpha_length&&stride>=480&&memory>=std::uint64_t(stride)*320;
    }
};
inline std::string unique_display(std::span<const DisplayFormat> displays) {
    std::string result;
    for(const auto& display:displays) if(display.supported()) {
        if(!result.empty()) throw std::runtime_error("multiple compatible LCDs; set --fb explicitly");
        result=display.path;
    }
    if(result.empty()) throw std::runtime_error("no compatible 240x320 RGB565 LCD; check /proc/fb and permissions");
    return result;
}
}
