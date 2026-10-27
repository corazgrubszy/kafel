#pragma once
#include "system/system_snapshot.hpp"
namespace kafel {
struct CpuTicks {
    std::uint64_t total, idle;
};
std::optional<CpuTicks> parse_cpu_ticks(std::string_view text) noexcept;
std::optional<unsigned> cpu_usage(CpuTicks previous, CpuTicks current) noexcept;
std::optional<unsigned> parse_ram_usage(std::string_view text) noexcept;
std::optional<unsigned> storage_usage(std::uint64_t total, std::uint64_t free) noexcept;
std::optional<int> parse_temperature(std::string_view text) noexcept;
FixedText<16> default_ipv4_interface(std::string_view routes) noexcept;
std::optional<FixedText<65>> format_calendar_date(unsigned year, unsigned month, unsigned day,
                                                  unsigned weekday) noexcept;
struct SystemDisplay {
    FixedText<8> cpu, ram;
    FixedText<20> free_space;
    void update(const SystemSnapshot &snapshot) noexcept;
};
} // namespace kafel
