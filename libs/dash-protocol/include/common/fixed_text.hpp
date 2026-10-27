#pragma once
#include <algorithm>
#include <array>
#include <string_view>
namespace kafel {
template <std::size_t N> struct FixedText {
    static_assert(N > 0);
    std::array<char, N> bytes{};
    void assign(std::string_view value) noexcept {
        std::array<char, N> next{};
        const auto size = std::min(value.size(), N - 1);
        if (size)
            std::copy_n(value.begin(), size, next.begin());
        bytes = next;
    }
    std::string_view view() const noexcept {
        return {bytes.data(), std::char_traits<char>::length(bytes.data())};
    }
};
} // namespace kafel
