#pragma once
#include "common/fixed_text.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kafel {
// No OS handles, formatting or owning heap strings. Missing values stay missing.
struct SystemSnapshot {
    FixedText<65> hostname;
    FixedText<16> ipv4;
    std::optional<unsigned> cpu_percent, ram_percent, storage_percent;
    std::optional<std::uint64_t> storage_free_bytes;
    std::optional<int> temperature_millidegrees;
    bool storage_emmc = false;
};
struct ClockTime {
    unsigned hour, minute;
};
} // namespace kafel
