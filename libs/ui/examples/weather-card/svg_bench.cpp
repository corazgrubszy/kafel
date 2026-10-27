#include "svg_memory_stats.hpp"
#include "weather_card.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t peak_rss() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage))
        return 0;
#ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
}
struct Timing {
    double median, p95;
};
template <class F> Timing measure(int count, F operation) {
    std::vector<double> samples;
    samples.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto start = Clock::now();
        operation();
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return {samples[samples.size() / 2], samples[(samples.size() - 1) * 95 / 100]};
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 5)
        return 2;
    try {
        const auto baseline = peak_rss();
        const auto source = ui::read_svg_file(argv[4]);
        const auto parse = measure(100, [&] { ui::SvgDocument parsed(source); });
        double cold = 0;
        Timing raster{};
        std::uint64_t svg_peak = 0;
        std::size_t svg_heap = 0, warm_allocations = 0;
        {
            ui::SvgDocument document(source);
            ui::SvgRasterizer rasterizer;
            std::vector<unsigned char> rgba(240 * 240 * 4);
            const auto start = Clock::now();
            rasterizer.render(document, 240, 240, rgba);
            cold = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
            const auto before_raster = ui::detail::svg_memory_stats().allocations;
            raster = measure(300, [&] { rasterizer.render(document, 240, 240, rgba); });
            svg_heap = ui::detail::svg_memory_stats().live;
            warm_allocations = ui::detail::svg_memory_stats().allocations - before_raster;
            svg_peak = peak_rss();
        }
        const auto setup = Clock::now();
        weather_example::WeatherCard card(argv[1], argv[2], argv[3], argv[4]);
        const auto setup_ms = std::chrono::duration<double, std::milli>(Clock::now() - setup).count();
        std::vector<unsigned char> pixels(240 * 240 * 3);
        ui::Surface surface(240, 240, pixels);
        card.paint(surface);
        const auto before_cached = ui::detail::svg_memory_stats().allocations;
        const auto cached = measure(200, [&] { card.paint(surface); });
        const auto cached_allocations = ui::detail::svg_memory_stats().allocations - before_cached;
        const auto uncached = measure(100, [&] {
            card.set_svg(source);
            card.paint(surface);
        });
        const auto svg_memory = ui::detail::svg_memory_stats();
        std::printf("{\"nanosvg_heap_peak_bytes\":%zu,\"nanosvg_live_svg_stage_bytes\":%zu,"
                    "\"nanosvg_live_after_cache_bytes\":%zu,\"warm_raster_allocation_calls\":%zu,"
                    "\"cached_paint_svg_allocation_calls\":%zu,",
                    svg_memory.peak, svg_heap, svg_memory.live, warm_allocations, cached_allocations);
        std::printf("\"svg_bytes\":%zu,\"rgba_cache_bytes\":230400,\"rgb_frame_bytes\":172800,"
                    "\"parse_destroy_us\":{\"median\":%.3f,\"p95\":%.3f},\"cold_raster_us\":%.3f,"
                    "\"warm_raster_us\":{\"median\":%.3f,\"p95\":%.3f},\"font_and_card_setup_ms\":%.3f,"
                    "\"cached_card_us\":{\"median\":%.3f,\"p95\":%.3f},\"uncached_card_us\":{\"median\":%.3f,"
                    "\"p95\":%.3f},"
                    "\"process_peak_rss_baseline_bytes\":%llu,\"process_peak_rss_after_svg_bytes\":%llu,"
                    "\"process_peak_rss_full_card_bytes\":%llu}\n",
                    source.size(), parse.median, parse.p95, cold, raster.median, raster.p95, setup_ms,
                    cached.median, cached.p95, uncached.median, uncached.p95,
                    static_cast<unsigned long long>(baseline), static_cast<unsigned long long>(svg_peak),
                    static_cast<unsigned long long>(peak_rss()));
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
