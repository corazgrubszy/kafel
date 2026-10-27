#include "platform/linux/linux_system_probe.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace kafel::linux_device {
namespace {
template <std::size_t N> std::string_view read_file(const char *path, std::array<char, N> &buffer) noexcept {
    auto *file = std::fopen(path, "r");
    if (!file)
        return {};
    const auto size = std::fread(buffer.data(), 1, N - 1, file);
    const bool error = std::ferror(file);
    std::fclose(file);
    return error ? std::string_view{} : std::string_view(buffer.data(), size);
}
} // namespace
SystemSnapshot LinuxSystemProbe::sample(std::uint64_t now_ms) noexcept {
    std::array<char, 4096> buffer{};
    const auto current = parse_cpu_ticks(read_file("/proc/stat", buffer));
    cached_.cpu_percent = current && previous_cpu_ ? cpu_usage(*previous_cpu_, *current) : std::nullopt;
    previous_cpu_ = current;
    cached_.ram_percent = parse_ram_usage(read_file("/proc/meminfo", buffer));
    cached_.temperature_millidegrees =
        parse_temperature(read_file("/sys/class/thermal/thermal_zone0/temp", buffer));
    if (!sampled_ || now_ms < slow_at_ || now_ms - slow_at_ >= 10000) {
        refresh_slow();
        slow_at_ = now_ms;
        sampled_ = true;
    }
    return cached_;
}
void LinuxSystemProbe::refresh_slow() noexcept {
    cached_.hostname.bytes = {};
    if (gethostname(cached_.hostname.bytes.data(), cached_.hostname.bytes.size()) != 0)
        cached_.hostname.assign("--");
    struct statvfs fs{};
    cached_.storage_free_bytes.reset();
    cached_.storage_percent.reset();
    if (statvfs("/", &fs) == 0 && fs.f_frsize && fs.f_bavail <= UINT64_MAX / fs.f_frsize)
        cached_.storage_free_bytes = static_cast<std::uint64_t>(fs.f_bavail) * fs.f_frsize;
    if (fs.f_blocks)
        cached_.storage_percent = storage_usage(fs.f_blocks, fs.f_bfree);
    cached_.storage_emmc = false;
    struct stat root{};
    if (stat("/", &root) == 0) {
        std::array<char, 64> buffer{};
        char path[128];
        for (const char *suffix : {"/device/type", "/../device/type"}) {
            std::snprintf(path, sizeof(path), "/sys/dev/block/%u:%u%s", major(root.st_dev),
                          minor(root.st_dev), suffix);
            if (read_file(path, buffer).starts_with("MMC")) {
                cached_.storage_emmc = true;
                break;
            }
        }
    }
    // Prefer the interface of the default route, then a stable lexical fallback.
    std::array<char, 4096> routes{};
    const auto preferred = default_ipv4_interface(read_file("/proc/net/route", routes));
    cached_.ipv4.assign("");
    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0)
        return;
    char chosen[IFNAMSIZ]{};
    for (auto *item = interfaces; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || !(item->ifa_flags & IFF_UP) ||
            (item->ifa_flags & IFF_LOOPBACK))
            continue;
        const bool is_preferred = std::strcmp(item->ifa_name, preferred.bytes.data()) == 0;
        const bool chosen_preferred = std::strcmp(chosen, preferred.bytes.data()) == 0;
        if (*chosen && (chosen_preferred || (!is_preferred && std::strcmp(item->ifa_name, chosen) >= 0)))
            continue;
        char address[INET_ADDRSTRLEN]{};
        const auto *ip = reinterpret_cast<const sockaddr_in *>(item->ifa_addr);
        if (inet_ntop(AF_INET, &ip->sin_addr, address, sizeof(address))) {
            cached_.ipv4.assign(address);
            std::snprintf(chosen, sizeof(chosen), "%s", item->ifa_name);
        }
    }
    freeifaddrs(interfaces);
}
} // namespace kafel::linux_device
