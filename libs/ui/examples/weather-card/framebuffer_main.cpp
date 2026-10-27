#include "platform/linux/framebuffer.hpp"
#include "weather_card.hpp"
#include <array>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <poll.h>
#include <string_view>
namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
} // namespace
int main(int argc, char **argv) {
    if (argc != 6)
        return 2;
    try {
        std::array<unsigned char, 240 * 240 * 3> rgb{};
        ui::Surface surface(240, 240, rgb);
        weather_example::WeatherCard card(argv[1], argv[2], argv[3], argv[4]);
        card.paint(surface);
        const auto path = kafel::linux_device::Framebuffer::resolve("auto");
        if (std::string_view(argv[5]) == "--check") {
            std::puts("READY weather-card-preview: fonts, render and LCD validated");
            return 0;
        }
        int seconds = 0;
        const std::string_view duration = argv[5];
        const auto parsed = std::from_chars(duration.data(), duration.data() + duration.size(), seconds);
        if (parsed.ec != std::errc{} || parsed.ptr != duration.data() + duration.size() || seconds < 5 ||
            seconds > 600)
            return 2;
        std::array<kafel::Pixel, 240 * 320> frame{};
        for (std::size_t i = 0; i < 240 * 240; ++i)
            frame[i] = kafel::rgb(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);
        kafel::linux_device::Framebuffer framebuffer(path.c_str());
        framebuffer.present(frame);
        std::puts("READY weather-card-preview: frame presented; bottom 80 px blank");
        std::fflush(stdout);
        // One frame, then sleep while retaining the framebuffer lock.
        for (int elapsed = 0; elapsed < seconds && !stopping; ++elapsed)
            poll(nullptr, 0, 1000);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
