#pragma once
#include <cstdint>
#include <optional>

namespace kafel {
// Stateless, allocation-free overflow scrolling. Caller owns text and elapsed
// time, and can pause the animation by keeping elapsed_ms unchanged.
class MarqueeText {
public:
    static constexpr unsigned pause_ms = 1200;
    static constexpr unsigned pixels_per_second = 24;
    static std::optional<std::uint64_t> next_change(int text_width,int viewport_width,std::uint64_t elapsed_ms) noexcept;
    static int offset(int text_width, int viewport_width, std::uint64_t elapsed_ms) noexcept;
};
}
