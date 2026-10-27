#pragma once
#include "graphics/surface.hpp"
#include <cerrno>
#include <span>
#include <stdexcept>
#include <system_error>
namespace kafel::linux_device {
// Writer(span<byte>, byte offset) has pwrite semantics. A packed framebuffer
// needs one write; padding stays untouched on devices with a larger stride.
template <class Write> void write_frame(std::span<const Pixel> pixels, unsigned stride, Write write) {
    constexpr unsigned width = 240, height = 320, row_bytes = width * sizeof(Pixel);
    if (pixels.size() != width * height || stride < row_bytes)
        throw std::invalid_argument("frame size/stride");
    auto all = [&](std::span<const std::byte> bytes, std::size_t offset) {
        while (!bytes.empty()) {
            const auto n = write(bytes, offset);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                throw std::system_error(errno, std::generic_category(), "write framebuffer");
            if (n == 0 || static_cast<std::size_t>(n) > bytes.size())
                throw std::runtime_error("framebuffer write made no progress");
            bytes = bytes.subspan(static_cast<std::size_t>(n));
            offset += static_cast<std::size_t>(n);
        }
    };
    if (stride == row_bytes)
        all(std::as_bytes(pixels), 0);
    else
        for (unsigned y = 0; y < height; ++y)
            all(std::as_bytes(pixels.subspan(y * width, width)), static_cast<std::size_t>(y) * stride);
}
} // namespace kafel::linux_device
