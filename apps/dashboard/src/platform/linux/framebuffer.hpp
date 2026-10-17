#pragma once
#include "graphics/surface.hpp"
#include <span>
#include "display_format.hpp"

namespace kafel::linux_device {
// Owns the descriptor and advisory lock. Presentation copies complete rows;
// no device mapping or allocation is needed during a frame.
class Framebuffer {
public:
    explicit Framebuffer(const char* path);
    ~Framebuffer();
    static DisplayFormat describe(const char* path);
    static std::string resolve(const char* requested);
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    void present(std::span<const Pixel> pixels);
private:
    int fd_ = -1;
    unsigned stride_ = 0;
};
}
