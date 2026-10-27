#include "platform/linux/framebuffer.hpp"
#include "frame_writer.hpp"
#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <linux/fb.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace kafel::linux_device {
namespace {
[[noreturn]] void fail(const char *operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
} // namespace
namespace {
DisplayFormat describe_fd(int fd, const char *path) {
    fb_fix_screeninfo f{};
    fb_var_screeninfo v{};
    if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0)
        fail("framebuffer info");
    DisplayFormat d;
    d.path = path;
    d.driver.assign(f.id, strnlen(f.id, sizeof(f.id)));
    d.width = v.xres;
    d.height = v.yres;
    d.bpp = v.bits_per_pixel;
    d.type = f.type;
    d.visual = f.visual;
    d.stride = f.line_length;
    d.memory = f.smem_len;
    d.xoffset = v.xoffset;
    d.yoffset = v.yoffset;
    d.nonstd = v.nonstd;
    d.grayscale = v.grayscale;
    d.red_offset = v.red.offset;
    d.red_length = v.red.length;
    d.red_msb = v.red.msb_right;
    d.green_offset = v.green.offset;
    d.green_length = v.green.length;
    d.green_msb = v.green.msb_right;
    d.blue_offset = v.blue.offset;
    d.blue_length = v.blue.length;
    d.blue_msb = v.blue.msb_right;
    d.alpha_length = v.transp.length;
    return d;
}
} // namespace
DisplayFormat Framebuffer::describe(const char *path) {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        fail("inspect framebuffer");
    try {
        auto result = describe_fd(fd, path);
        close(fd);
        return result;
    } catch (...) {
        close(fd);
        throw;
    }
}
std::string Framebuffer::resolve(const char *requested) {
    if (std::string_view(requested) != "auto") {
        const auto d = describe(requested);
        if (!d.supported())
            throw std::runtime_error("expected 240x320 little-endian RGB565 framebuffer");
        return requested;
    }
    std::vector<DisplayFormat> displays;
    for (const auto &entry : std::filesystem::directory_iterator("/sys/class/graphics")) {
        const auto name = entry.path().filename().string();
        if (name.size() < 3 || !name.starts_with("fb") ||
            name.find_first_not_of("0123456789", 2) != name.npos)
            continue;
        const auto device = "/dev/" + name;
        // Inspection errors are not silently skipped: otherwise an inaccessible
        // second compatible LCD could make automatic selection ambiguous.
        displays.push_back(describe(device.c_str()));
    }
    return unique_display(displays);
}
Framebuffer::Framebuffer(const char *requested) {
    if (std::endian::native != std::endian::little)
        throw std::runtime_error("little-endian framebuffer required");
    const auto path = resolve(requested);
    fd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
        fail("open framebuffer");
    try {
        if (flock(fd_, LOCK_EX | LOCK_NB) < 0)
            fail("framebuffer already locked");
        const auto d = describe_fd(fd_, path.c_str());
        if (!d.supported())
            throw std::runtime_error("framebuffer format changed during startup");
        stride_ = d.stride;
        std::fprintf(stderr, "fb: %s %s %ux%u bpp=%u stride=%u\n", path.c_str(), d.driver.c_str(), d.width,
                     d.height, d.bpp, d.stride);
    } catch (...) {
        close(fd_);
        fd_ = -1;
        throw;
    }
}
Framebuffer::~Framebuffer() {
    if (fd_ >= 0)
        close(fd_);
}
void Framebuffer::present(std::span<const Pixel> pixels) {
    write_frame(pixels, stride_, [this](std::span<const std::byte> bytes, std::size_t offset) {
        return pwrite(fd_, bytes.data(), bytes.size(), static_cast<off_t>(offset));
    });
}
} // namespace kafel::linux_device
