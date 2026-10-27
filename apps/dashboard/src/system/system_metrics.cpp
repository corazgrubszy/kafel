#include "system/system_metrics.hpp"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace kafel {
namespace {
void trim(std::string_view &text) noexcept {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' || text.front() == '\r'))
        text.remove_prefix(1);
}
std::optional<std::uint64_t> number(std::string_view &text) noexcept {
    trim(text);
    if (text.empty())
        return {};
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{})
        return {};
    if (result.ptr != text.data() + text.size() && *result.ptr != ' ' && *result.ptr != '\t' &&
        *result.ptr != '\n' && *result.ptr != '\r')
        return {};
    text.remove_prefix(static_cast<std::size_t>(result.ptr - text.data()));
    return value;
}
std::optional<std::uint64_t> memory_field(std::string_view text, std::string_view key) noexcept {
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        if (line.starts_with(key)) {
            line.remove_prefix(key.size());
            auto result = number(line);
            trim(line);
            if (line != "kB" && line != "kB\r")
                return {};
            return result;
        }
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return {};
}
} // namespace
std::optional<CpuTicks> parse_cpu_ticks(std::string_view text) noexcept {
    if (!text.starts_with("cpu "))
        return {};
    text = text.substr(4, text.find('\n') == std::string_view::npos ? text.size() - 4 : text.find('\n') - 4);
    std::array<std::uint64_t, 8> values{};
    for (auto &value : values) {
        auto parsed = number(text);
        if (!parsed)
            return {};
        value = *parsed;
    }
    std::uint64_t total = 0;
    for (auto value : values) {
        if (value > UINT64_MAX - total)
            return {};
        total += value;
    }
    // guest/guest_nice are already included in user/nice; do not count twice.
    return CpuTicks{total, values[3] + values[4]};
}
std::optional<unsigned> cpu_usage(CpuTicks previous, CpuTicks current) noexcept {
    if (current.total <= previous.total || current.idle < previous.idle)
        return {};
    const auto total = current.total - previous.total, idle = current.idle - previous.idle;
    if (idle > total)
        return {};
    return static_cast<unsigned>(
        std::lround(100.0 * static_cast<double>(total - idle) / static_cast<double>(total)));
}
std::optional<unsigned> parse_ram_usage(std::string_view text) noexcept {
    const auto total = memory_field(text, "MemTotal:"), available = memory_field(text, "MemAvailable:");
    if (!total || !available || !*total || *available > *total)
        return {};
    return static_cast<unsigned>(
        std::lround(100.0 * static_cast<double>(*total - *available) / static_cast<double>(*total)));
}
std::optional<unsigned> storage_usage(std::uint64_t total, std::uint64_t free) noexcept {
    if (!total || free > total)
        return {};
    return static_cast<unsigned>(
        std::lround(100.0 * static_cast<double>(total - free) / static_cast<double>(total)));
}
std::optional<int> parse_temperature(std::string_view text) noexcept {
    trim(text);
    if (text.empty())
        return {};
    int value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{})
        return {};
    text.remove_prefix(static_cast<std::size_t>(result.ptr - text.data()));
    trim(text);
    if (!text.empty() || value < -40000 || value > 150000)
        return {};
    return value;
}
FixedText<16> default_ipv4_interface(std::string_view routes) noexcept {
    FixedText<16> selected;
    unsigned best = std::numeric_limits<unsigned>::max();
    while (!routes.empty()) {
        const auto end = routes.find('\n');
        const auto line = routes.substr(0, end);
        if (line.size() < 256) {
            std::array<char, 256> copy{};
            std::copy(line.begin(), line.end(), copy.begin());
            char name[16]{};
            unsigned destination = 1, gateway = 0, flags = 0, refs = 0, use = 0, metric = 0, mask = 1;
            if (std::sscanf(copy.data(), "%15s %x %x %x %u %u %u %x", name, &destination, &gateway, &flags,
                            &refs, &use, &metric, &mask) == 8 &&
                destination == 0 && mask == 0 && (flags & 1) &&
                (selected.view().empty() || metric < best ||
                 (metric == best && std::string_view(name) < selected.view()))) {
                selected.assign(name);
                best = metric;
            }
        }
        if (end == std::string_view::npos)
            break;
        routes.remove_prefix(end + 1);
    }
    return selected;
}
std::optional<FixedText<65>> format_calendar_date(unsigned year, unsigned month, unsigned day,
                                                  unsigned weekday) noexcept {
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 || weekday > 6)
        return {};
    constexpr std::array<unsigned, 12> lengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > lengths[month - 1] + (month == 2 && leap ? 1U : 0U))
        return {};
    constexpr std::array days = {"Niedziela", "Poniedziałek", "Wtorek", "Środa",
                                 "Czwartek",  "Piątek",       "Sobota"};
    constexpr std::array months = {"sty", "lut", "mar", "kwi", "maj", "cze",
                                   "lip", "sie", "wrz", "paź", "lis", "gru"};
    FixedText<65> next;
    std::snprintf(next.bytes.data(), next.bytes.size(), "%s %02u %s", days[weekday], day, months[month - 1]);
    return next;
}
void SystemDisplay::update(const SystemSnapshot &snapshot) noexcept {
    auto percent = [](auto &output, auto value) {
        if (value)
            std::snprintf(output.bytes.data(), output.bytes.size(), "%u%%", std::min(100U, *value));
        else
            output.assign("--%");
    };
    percent(cpu, snapshot.cpu_percent);
    percent(ram, snapshot.ram_percent);
    if (!snapshot.storage_free_bytes)
        free_space.assign("-- GB");
    else {
        const auto bytes = *snapshot.storage_free_bytes;
        const auto unit = bytes >= 1000000000000ULL ? 1000000000000ULL : 1000000000ULL;
        std::snprintf(free_space.bytes.data(), free_space.bytes.size(), "%llu,%llu %s",
                      static_cast<unsigned long long>(bytes / unit),
                      static_cast<unsigned long long>((bytes % unit) / (unit / 10)),
                      unit == 1000000000ULL ? "GB" : "TB");
    }
}
} // namespace kafel
