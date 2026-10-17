#include "source.hpp"
#include "packet.hpp"
#include "services/service_registry.hpp"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const net::HttpClient client;
    const auto result = kafel::weather::fetch(client, argv[1]);
    if (const auto* conditions = std::get_if<kafel::weather::Conditions>(&result)) {
        kafel::weather::WeatherCache cache;
        const auto now = kafel::weather::Clock::now();
        cache.update(*conditions, now);
        const auto packet = kafel::weather::dashboard_packet(cache.snapshot(now));
        if (!kafel::parse_service_message(packet)) return 3;
        std::printf("%s", packet.c_str());
        return 0;
    }
    std::fprintf(stderr, "%s\n", std::get<kafel::weather::FetchError>(result).message.c_str());
    return 1;
}
