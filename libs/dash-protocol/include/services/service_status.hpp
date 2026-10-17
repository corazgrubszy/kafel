#pragma once
#include <string_view>
#include <cstddef>
namespace kafel {
inline constexpr std::size_t service_detail_bytes=512;
inline constexpr std::size_t service_packet_bytes=8192;
inline constexpr std::size_t service_tile_capacity=13;
// A read-only transfer value. Consumers copy text they need to retain.
struct ServiceStatus {
    std::string_view name, detail, status;
    bool alarm=false;
    bool attention=false;
};
}
