#include "packet.hpp"
#include "source.hpp"

#include <cstdio>
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    const net::HttpClient client;
    const auto result = kafel::weather::fetch(client, argv[1]);
    if (const auto *conditions = std::get_if<kafel::weather::Conditions>(&result)) {
        std::fprintf(stderr, "DETAILS apparent=%.1f precipitation=%.1f hour_end=%lld code=%u is_day=%u\n",
                     conditions->apparent_temperature, conditions->precipitation_mm,
                     static_cast<long long>(conditions->precipitation_hour_end), conditions->weather_code,
                     conditions->is_day ? 1u : 0u);
        kafel::weather::WeatherCache cache;
        const auto now = kafel::weather::Clock::now();
        cache.update(*conditions, now);
        const auto packet = kafel::weather::dashboard_tiles(cache.snapshot(now));
        std::printf("%s", packet.c_str());
        return 0;
    }
    std::fprintf(stderr, "%s\n", std::get<kafel::weather::FetchError>(result).message.c_str());
    return 1;
}
