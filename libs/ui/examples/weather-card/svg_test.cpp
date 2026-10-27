#include "appearance.hpp"
#include "memory.hpp"
#include "ui/svg.hpp"
#include "weather_card.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL SVG: %s\n", message);
        std::exit(1);
    }
}
template <class F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    check(failed, "invalid SVG or buffer rejected");
}
} // namespace
void test_svg() {
    ui::SvgRasterizer rasterizer;
    ui::SvgDocument alpha(
        "<svg width='16' height='16'><rect width='16' height='16' fill='#ff0000' opacity='0.5'/></svg>");
    std::array<unsigned char, 16 * 16 * 4 + 64> guarded;
    guarded.fill(0xa5);
    auto pixels = std::span(guarded).first(16 * 16 * 4);
    rasterizer.render(alpha, 16, 16, pixels);
    check(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] >= 127 && pixels[3] <= 128,
          "straight-alpha opacity");
    check(std::all_of(guarded.end() - 64, guarded.end(), [](auto byte) { return byte == 0xa5; }),
          "RGBA guard intact");
    ui::SvgDocument gradient(
        "<svg width='16' height='16'><defs><linearGradient id='g' x1='0%' y1='0%' x2='100%' y2='0%'><stop "
        "offset='0' stop-color='#ff0000'/><stop offset='1' "
        "stop-color='#0000ff'/></linearGradient></defs><rect width='16' height='16' fill='url(#g)'/></svg>");
    rasterizer.render(gradient, 16, 16, pixels);
    check(pixels[0] > pixels[2] && pixels[15 * 4] < pixels[15 * 4 + 2],
          "linear gradient is actually rasterized");
    const auto first = guarded;
    rasterizer.render(gradient, 16, 16, pixels);
    check(guarded == first, "deterministic rasterization and scratch reuse");
    ui::SvgDocument radial(
        "<svg width='16' height='16'><defs><radialGradient id='g'><stop offset='0' "
        "stop-color='#ffffff'/><stop offset='1' stop-color='#000000'/></radialGradient></defs><rect "
        "width='16' height='16' fill='url(#g)'/></svg>");
    rasterizer.render(radial, 16, 16, pixels);
    check(pixels[(8 * 16 + 8) * 4] > pixels[0], "radial gradient");
    ui::SvgDocument circle(
        "<svg width='16' height='16'><circle cx='8' cy='8' r='5.2' fill='#ffffff'/></svg>");
    rasterizer.render(circle, 16, 16, pixels);
    bool coverage = false;
    for (std::size_t i = 3; i < pixels.size(); i += 4)
        coverage |= pixels[i] > 0 && pixels[i] < 255;
    check(coverage && pixels[3] == 0, "antialiasing and transparent exterior");
    ui::SvgDocument letterbox(
        "<svg width='16' height='8'><rect width='16' height='8' fill='#00ff00'/></svg>");
    rasterizer.render(letterbox, 16, 16, pixels);
    check(pixels[3] == 0 && pixels[(8 * 16 + 8) * 4 + 1] == 255, "aspect ratio and centered viewport");
    ui::SvgDocument empty("<svg width='16' height='16'></svg>");
    rasterizer.render(empty, 16, 16, pixels);
    check(std::all_of(pixels.begin(), pixels.end(), [](auto byte) { return byte == 0; }),
          "valid empty SVG clears previous pixels");
    rejects([&] { rasterizer.render(alpha, 0, 16, pixels); });
    rejects([&] { rasterizer.render(alpha, 513, 16, pixels); });
    rejects([&] { rasterizer.render(alpha, 16, 16, pixels.first(10)); });
    for (auto invalid :
         {"not svg", "<svg width='16' height='16'>", "<svg width='0' height='16'></svg>",
          "<svg width='16' height='16'><g></svg>", "<svg width='16' height='16'><text>x</text></svg>",
          "<svg width='16' height='16'><image href='https://example.invalid/a.png'/></svg>",
          "<svg width='16' height='16'><script/></svg>",
          "<svg width='16' height='16' filter='url(#f)'></svg>",
          "<!DOCTYPE svg><svg width='16' height='16'/>", "<svg width='1e99' height='16'/>",
          "<svg width='16' height='16'/><svg width='16' height='16'/>"})
        rejects([&] { ui::SvgDocument rejected(invalid); });
    rejects([] { ui::SvgDocument rejected(std::string(ui::SvgDocument::maximum_bytes + 1, ' ')); });
    rejects([] { ui::SvgDocument rejected(std::string("<svg/>\0trailing", 15)); });
    // These documents pass the raw 1e6 coordinate limit, but would overflow
    // NanoSVG's fixed-point conversion after scaling into a 240px viewport.
    std::vector<unsigned char> large_pixels(240 * 240 * 4, 0xa5);
    for (auto unsafe :
         {"<svg width='1' height='1'><path d='M999999 0L1000000 0L1000000 1L999999 1Z' "
          "fill='#ffffff'/></svg>",
          "<svg width='1' height='1'><path d='M-999999 0L-1000000 0L-1000000 1L-999999 1Z' "
          "fill='#ffffff'/></svg>",
          "<svg width='1' height='1'><rect width='1' height='1' transform='translate(999999 0)'/></svg>",
          "<svg width='1' height='1'><path d='M0 0L1 1' stroke='#ffffff' stroke-width='1000000' "
          "stroke-linecap='round'/></svg>",
          "<svg width='1' height='1'><path d='M0 0L1 1L0 1' stroke='#ffffff' stroke-width='1' "
          "stroke-miterlimit='1000000'/></svg>"}) {
        ui::SvgDocument document(unsafe);
        rejects([&] { rasterizer.render(document, 240, 240, large_pixels); });
        check(std::all_of(large_pixels.begin(), large_pixels.end(), [](auto byte) { return byte == 0xa5; }),
              "unsafe scaled geometry is rejected before touching output");
    }
    for (auto attribute :
         {"stroke-miterlimit='1e99'", "stroke-dasharray='1e99 1'", "stroke-dashoffset='1e99'"}) {
        const auto source =
            std::string("<svg width='16' height='16'><path d='M0 0L16 16' stroke='#ffffff' ") + attribute +
            "/></svg>";
        rejects([&] { ui::SvgDocument document(source); });
    }
    // A shallow edge can have an unbounded slope despite tiny coordinates.
    // It spans only one vertical sample; the vendor patch skips unused dx,
    // while preserving the true first intersection and normal antialiasing.
    ui::SvgDocument shallow(
        "<svg width='1' height='1'><path d='M0 0.00041666665L1 0.0004166667L0 1Z' fill='#ffffff'/></svg>");
    rasterizer.render(shallow, 240, 240, large_pixels);
    check(large_pixels[3] > 0 && large_pixels[(239 * 240 + 239) * 4 + 3] == 0,
          "near-horizontal edge renders safely at its actual intersection");
    const auto shallow_first = large_pixels;
    rasterizer.render(shallow, 240, 240, large_pixels);
    check(large_pixels == shallow_first, "shallow-edge raster remains deterministic");
    ui::SvgDocument size_dependent("<svg width='1' height='1'><rect x='3000' width='1' height='1'/></svg>");
    rasterizer.render(size_dependent, 16, 16, pixels);
    check(std::all_of(pixels.begin(), pixels.end(), [](auto byte) { return byte == 0; }),
          "safe off-canvas geometry is still clipped normally");
    std::vector<unsigned char> maximum_pixels(512 * 512 * 4, 0xa5);
    rejects([&] { rasterizer.render(size_dependent, 512, 512, maximum_pixels); });
    ui::SvgDocument clipped(
        "<svg width='16' height='16'><rect x='-4' y='-4' width='20' height='20' fill='#00ff00'/></svg>");
    rasterizer.render(clipped, 16, 16, pixels);
    check(pixels[1] == 255 && pixels[3] == 255, "ordinary partially clipped artwork stays unchanged");
    const auto catalog = kafel::weather::appearance_catalog();
    constexpr std::array codes = {0u,  1u,  2u,  3u,  45u, 48u, 51u, 53u, 55u, 56u, 57u, 61u, 63u, 65u, 66u,
                                  67u, 71u, 73u, 75u, 77u, 80u, 81u, 82u, 85u, 86u, 95u, 96u, 97u, 99u};
    check(catalog.size() == codes.size(), "all documented Open-Meteo WMO codes, including 97");
    for (std::size_t i = 0; i < codes.size(); ++i) {
        check(catalog[i].code == codes[i], "catalog complete, unique and ordered");
        for (bool day : {false, true}) {
            const auto look = kafel::weather::appearance(codes[i], day);
            check(look.illustration == (day ? catalog[i].day : catalog[i].night),
                  "day/night resource selection");
            check(!look.description.empty() && look.illustration != "not-available",
                  "known WMO is never a fallback");
        }
    }
    check(kafel::weather::appearance(0, true).illustration == "clear-day" &&
              kafel::weather::appearance(0, false).illustration == "clear-night",
          "clear sky switches sun/moon");
    check(kafel::weather::appearance(2, false).illustration == "partly-cloudy-night",
          "partial clouds at night");
    check(kafel::weather::appearance(97, true).illustration == "thunderstorms-extreme-day-rain",
          "heavy thunderstorm distinct");
    check(kafel::weather::appearance(99, false).illustration == "thunderstorms-extreme-night-hail",
          "heavy hail at night");
    for (unsigned code = 0; code < 110; ++code) {
        const bool known = std::find(codes.begin(), codes.end(), code) != codes.end();
        for (bool day : {false, true})
            check((kafel::weather::appearance(code, day).illustration == "not-available") == !known,
                  "unknown WMO explicit for both day/night");
    }
    std::puts("PASS SVG: gradients, straight alpha, AA, viewport, buffers, unsupported features, "
              "deterministic rasterization and weather-only WMO mapping");
}

void test_weather_svg_scenes(const char *regular, const char *bold, const char *json, const char *initial_svg,
                             const char *icons_path) {
    struct Scene {
        const char *resource;
        weather_example::Data data;
        std::uint64_t expected;
    };
    const std::array scenes = {
        Scene{"sun", {22, 24, 2.5, 0, "Słonecznie", 0}, 0x103a457be43fb840ull},
        Scene{"clouds", {13, 11, 5, 0, "Pochmurno", 3}, 0xd52bee48206e4fb2ull},
        Scene{"rain", {9, 6, 7.2, 2.4, "Deszcz", 63}, 0xd21bfaa9fa8a002cull},
        Scene{"snow", {-2, -6, 4.1, 0.8, "Śnieg", 73}, 0x528bc086f66aebfaull},
    };
    weather_example::WeatherCard card(regular, bold, json, initial_svg);
    std::array<unsigned char, 240 * 240 * 3> pixels{};
    ui::Surface surface(240, 240, pixels);
    const auto directory = std::filesystem::path(initial_svg).parent_path();
    for (const auto &scene : scenes) {
        const auto path = directory / (std::string(scene.resource) + ".svg");
        card.set_svg(ui::read_svg_file(path.c_str()));
        const auto allocations = ui_test::memory().allocations;
        card.paint(surface, scene.data);
        check(allocations == ui_test::memory().allocations,
              "each cached SVG card paints without C++ allocations");
        std::uint64_t hash = 14695981039346656037ull;
        for (auto byte : pixels)
            hash = (hash ^ byte) * 1099511628211ull;
        check(hash == scene.expected, "four real SVG previews match reviewed RGB references");
    }
    const auto resources = std::filesystem::path(icons_path);
    std::vector<unsigned char> day_pixels(128 * 128 * 4), night_pixels(128 * 128 * 4);
    ui::SvgRasterizer rasterizer;
    for (const auto &entry : kafel::weather::appearance_catalog()) {
        const auto day_path = resources / (std::string(entry.day) + ".svg");
        const auto night_path = resources / (std::string(entry.night) + ".svg");
        ui::SvgDocument day(ui::read_svg_file(day_path.c_str())),
            night(ui::read_svg_file(night_path.c_str()));
        rasterizer.render(day, 128, 128, day_pixels);
        rasterizer.render(night, 128, 128, night_pixels);
        check(std::any_of(day_pixels.begin(), day_pixels.end(), [](auto b) { return b != 0; }),
              "every WMO SVG is nonempty and supported");
        check((day_pixels == night_pixels) == (entry.day == entry.night),
              "day/night pixels differ exactly when assets differ");
    }
    ui::SvgDocument unknown(ui::read_svg_file((resources / "not-available.svg").c_str()));
    rasterizer.render(unknown, 128, 128, day_pixels);
    std::puts("PASS all Flat resources: every WMO day/night parses and renders; distinct night pixels and "
              "explicit unknown");
    std::puts("PASS four weather SVG scenes: service-side WMO selection, reviewed RGB frames and "
              "allocation-free cached paint");
}
