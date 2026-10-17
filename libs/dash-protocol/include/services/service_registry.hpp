#pragma once
#include "services/service_status.hpp"
#include "services/metric_content.hpp"
#include "common/fixed_text.hpp"
#include <cstdint>
#include <optional>
#include <span>

namespace kafel {
struct ServiceTileMessage {
    enum class State { Ok, Warning, Error };
    FixedText<33> id;
    FixedText<49> title;
    FixedText<service_detail_bytes+1> detail;
    unsigned span=3;
    State state=State::Ok;
    bool attention=false;
    std::optional<MetricMessage> metric{};
};
struct ServiceTileStatus {
    std::string_view service_id,tile_id;
    unsigned span=3;
    ServiceStatus content;
    std::optional<MetricContent> metric{};
};
struct ServiceTiles {
    std::array<ServiceTileStatus,service_tile_capacity> items{};
    std::size_t count=0;
    auto view() const noexcept { return std::span<const ServiceTileStatus>(items.data(),count); }
};
// Wire format is independent of its local Linux transport.
struct ServiceMessage {
    FixedText<33> id;
    FixedText<49> name;
    std::array<ServiceTileMessage,service_tile_capacity> tiles{};
    std::size_t tile_count=0;
};
std::optional<ServiceMessage> parse_service_message(std::string_view wire) noexcept;
class ServiceRegistry {
public:
    static constexpr std::size_t capacity=7;
    static constexpr std::uint64_t ttl_ms=15000;
    struct Update { std::size_t index; bool added, changed; };
    std::optional<Update> receive(const ServiceMessage&,std::uint64_t now_ms) noexcept;
    bool expire(std::size_t index,std::uint64_t now_ms) noexcept;
    ServiceTiles tiles() const noexcept;
    std::size_t size() const noexcept { return count_; }
    ServiceRegistry()=default;
    ServiceRegistry(const ServiceRegistry&)=delete;
    ServiceRegistry& operator=(const ServiceRegistry&)=delete;
private:
    struct Entry {
        ServiceMessage message;
        std::uint64_t seen_ms=0;
        bool offline=false;
    };
    std::array<Entry,capacity> entries_{};
    std::size_t count_=0;
};
}
