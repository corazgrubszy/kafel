#include "kafel_build_info.h"
#include "weather.hpp"
#include "platform/unix_publisher.hpp"
#include "source.hpp"
#include "packet.hpp"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
const char* environment(const char* key, const char* fallback = "") {
    const auto* value = std::getenv(key);
    return value ? value : fallback;
}
}
int main(int argc, char** argv) {
    using namespace kafel::weather;
    using namespace kafel::unix_transport;
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::puts("kafel-weather 0.1.0 DASH/4 git=" KAFEL_GIT_REVISION); return 0;
    }
    if (argc != 1) { std::fprintf(stderr, "usage: kafel-weather [--version]\n"); return 2; }
    const auto url = forecast_url(environment("KAFEL_WEATHER_LATITUDE"), environment("KAFEL_WEATHER_LONGITUDE"));
    if (!url) {
        std::fprintf(stderr, "Set valid KAFEL_WEATHER_LATITUDE [-90,90] and KAFEL_WEATHER_LONGITUDE [-180,180]\n");
        return 2;
    }
    const auto default_path = default_socket_path();
    const std::string path = environment("KAFEL_DASHBOARD_SOCKET", default_path.c_str());
    if (!valid_socket_path(path)) { std::fprintf(stderr, "Invalid dashboard socket path\n"); return 2; }
    std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
    net::HttpClient client(environment("KAFEL_WEATHER_CA_BUNDLE"));
    WeatherCache cache;
    RetrySchedule schedule;
    auto fetch_at = Clock::now(), publish_at = fetch_at;
    auto report_at = Clock::time_point::min();
    bool publisher_failed = false;
    std::fputs("READY kafel-weather: publisher started (DASH/4)\n",stderr);
    while (!stopping) {
        auto now = Clock::now();
        // Send before fetching; ordinary source failures do not suppress heartbeat.
        if (now >= publish_at) {
            const int error = publish(path, dashboard_packet(cache.snapshot(now)));
            if (error && (!publisher_failed || now >= report_at)) {
                std::fprintf(stderr, "dashboard publish unavailable: %s\n", std::strerror(error));
                report_at = now + std::chrono::minutes(1);
            } else if (!error && publisher_failed) std::fprintf(stderr, "dashboard publish restored\n");
            publisher_failed = error != 0;
            publish_at = now + std::chrono::seconds(5);
        }
        if (now >= fetch_at && !stopping) {
            const auto result = fetch(client, *url);
            const auto* conditions = std::get_if<Conditions>(&result);
            now = Clock::now();
            if (conditions) {
                cache.update(*conditions, now);
                std::fprintf(stderr, "weather cache updated\n");
                publish_at = now; // Expose successful refresh immediately.
            } else {
                std::fprintf(stderr, "weather refresh failed: %s\n", std::get<FetchError>(result).message.c_str());
            }
            fetch_at = now + schedule.next(conditions != nullptr);
        }
        const auto delay = std::chrono::ceil<std::chrono::milliseconds>(std::min(fetch_at, publish_at) - Clock::now());
        if (!stopping && delay.count() > 0) poll(nullptr, 0, static_cast<int>(std::min<long long>(delay.count(), 5000)));
    }
}
