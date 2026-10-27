#include "dash5/unix.hpp"
#include "kafel_build_info.h"
#include "packet.hpp"
#include "source.hpp"
#include "weather.hpp"
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>

namespace {
std::atomic<bool> stopping{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void stop(int) { stopping.store(true, std::memory_order_relaxed); }
const char *environment(const char *key, const char *fallback = "") {
    const auto *value = std::getenv(key);
    return value ? value : fallback;
}
} // namespace
int main(int argc, char **argv) {
    using namespace kafel::weather;
    using namespace kafel::dash5;
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::printf("kafel-weather 0.5.0-dev DASH/5 git=%s\n", kafel_git_revision());
        return 0;
    }
    if (argc != 1) {
        std::fprintf(stderr, "usage: kafel-weather [--version]\n");
        return 2;
    }
    const auto url =
        forecast_url(environment("KAFEL_WEATHER_LATITUDE"), environment("KAFEL_WEATHER_LONGITUDE"));
    if (!url) {
        std::fprintf(stderr,
                     "Set valid KAFEL_WEATHER_LATITUDE [-90,90] and KAFEL_WEATHER_LONGITUDE [-180,180]\n");
        return 2;
    }
    const auto default_path = default_socket_path();
    const std::string path = environment("KAFEL_DASHBOARD_SOCKET", default_path.c_str());
    if (!valid_socket_path(path)) {
        std::fprintf(stderr, "Invalid dashboard socket path\n");
        return 2;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    net::HttpClient client(environment("KAFEL_WEATHER_CA_BUNDLE"));
    Publisher publisher(path, "kafel-weather", "Pogoda", dashboard_assets().resources);
    WeatherCache cache;
    RetrySchedule schedule;
    auto fetch_at = Clock::now(), publish_at = fetch_at;
    auto report_at = Clock::time_point::min();
    std::optional<bool> published_stale;
    bool publisher_failed = true;
    bool publish_source_result = false;
    std::fputs("READY kafel-weather: publisher started (DASH/5)\n", stderr);
    while (!stopping) {
        auto now = Clock::now();
        // Send before fetching; ordinary source failures do not suppress heartbeat.
        if (now >= publish_at) {
            const auto snapshot = cache.snapshot(now);
            const bool update =
                publish_source_result || !published_stale || *published_stale != snapshot.stale;
            const int error =
                update ? publisher.publish(dashboard_document(snapshot)) : publisher.heartbeat();
            if (!error)
                published_stale = snapshot.stale;
            if (error && (!publisher_failed || now >= report_at)) {
                std::fprintf(stderr, "dashboard publish unavailable: %s (%s)\n", std::strerror(error),
                             publisher.error_message().c_str());
                report_at = now + std::chrono::minutes(1);
            } else if (!error && (publisher_failed || publish_source_result))
                std::fprintf(stderr, "PUBLISHED kafel-weather: DASH/5 snapshot accepted\n");
            if (!error)
                publish_source_result = false;
            publisher_failed = error != 0;
            publish_at = now + std::chrono::seconds(5);
        }
        if (now >= fetch_at && !stopping) {
            const auto result = fetch(client, *url);
            const auto *conditions = std::get_if<Conditions>(&result);
            now = Clock::now();
            if (conditions) {
                cache.update(*conditions, now);
                std::fprintf(stderr, "weather cache updated\n");
            } else {
                std::fprintf(stderr, "weather refresh failed: %s\n",
                             std::get<FetchError>(result).message.c_str());
            }
            publish_source_result = true;
            publish_at = now; // Confirm both successful data and explicit source-error states.
            fetch_at = now + schedule.next(conditions != nullptr);
        }
        const auto delay =
            std::chrono::ceil<std::chrono::milliseconds>(std::min(fetch_at, publish_at) - Clock::now());
        if (!stopping && delay.count() > 0)
            poll(nullptr, 0, static_cast<int>(std::min<long long>(delay.count(), 5000)));
    }
}
