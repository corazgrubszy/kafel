#include "dash5/unix.hpp"
#include "kafel_build_info.h"
#include "packet.hpp"
#include "schedule.hpp"
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
const char *environment(const char *name, const char *fallback = "") {
    const auto *value = std::getenv(name);
    return value ? value : fallback;
}
} // namespace
int main(int argc, char **argv) {
    using namespace kafel::waste;
    using namespace kafel::dash5;
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::printf("kafel-waste 0.5.0-dev DASH/5 git=%s\n", kafel_git_revision());
        return 0;
    }
    const bool check = argc == 3 && std::string_view(argv[1]) == "--check";
    if (argc != 1 && !check) {
        std::fprintf(stderr, "usage: kafel-waste [--version|--check FILE]\n");
        return 2;
    }
    const char *path = check ? argv[2] : environment("KAFEL_WASTE_SCHEDULE");
    if (!*path) {
        std::fprintf(stderr, "Set KAFEL_WASTE_SCHEDULE\n");
        return 2;
    }
    const auto default_path = default_socket_path();
    const std::string socket = environment("KAFEL_DASHBOARD_SOCKET", default_path.c_str());
    if (!valid_socket_path(socket)) {
        std::fprintf(stderr, "Invalid dashboard socket\n");
        return 2;
    }
    if (check) {
        Publisher publisher(socket, "kafel-waste", "Śmieci", dashboard_assets().resources);
        ScheduleCache cache;
        if (const auto error = cache.reload(path)) {
            std::fprintf(stderr, "%s\n", error->message.c_str());
            return 1;
        }
        const auto *schedule = cache.get();
        std::printf("Schedule validated: %zu pickup days\n", schedule->days.size());
        return 0;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    using Clock = std::chrono::steady_clock;
    Publisher publisher(socket, "kafel-waste", "Śmieci", dashboard_assets().resources);
    ScheduleCache cache;
    std::optional<Date> published_date;
    bool published = false;
    bool publisher_failed = true;
    bool publish_source_result = false;
    auto reload_at = Clock::now(), report_at = reload_at;
    std::fputs("READY kafel-waste: publisher started (DASH/5)\n", stderr);
    while (!stopping) {
        auto now = Clock::now();
        if (now >= reload_at) {
            const auto error = cache.reload(path);
            if (!error)
                std::fprintf(stderr, "waste schedule loaded: %zu pickup days\n", cache.get()->days.size());
            else
                std::fprintf(stderr, "waste reload failed: %s\n", error->message.c_str());
            publish_source_result = true;
            reload_at = Clock::now() + std::chrono::seconds(error ? 30 : 60);
        }
        const auto wall = std::time(nullptr);
        const auto today = wall == static_cast<std::time_t>(-1) ? std::optional<Date>{} : local_date(wall);
        const bool update = !published || publish_source_result || today != published_date;
        const int error =
            update ? publisher.publish(dashboard_document(today ? cache.get() : nullptr,
                                                          today.value_or(Date{}), cache.degraded()))
                   : publisher.heartbeat();
        if (!error) {
            published = true;
            published_date = today;
        }
        now = Clock::now();
        if (error && (!publisher_failed || now >= report_at)) {
            std::fprintf(stderr, "dashboard publish unavailable: %s (%s)\n", std::strerror(error),
                         publisher.error_message().c_str());
            report_at = now + std::chrono::minutes(1);
        } else if (!error && (publisher_failed || publish_source_result))
            std::fprintf(stderr, "PUBLISHED kafel-waste: DASH/5 snapshot accepted\n");
        if (!error)
            publish_source_result = false;
        publisher_failed = error != 0;
        // Poll wakes on signals; wall-clock dates are recomputed each heartbeat.
        if (!stopping)
            poll(nullptr, 0, 5000);
    }
}
