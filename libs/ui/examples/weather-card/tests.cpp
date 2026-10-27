#include "memory.hpp"
#include "weather_card.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
void test_svg();
void test_weather_svg_scenes(const char *, const char *, const char *, const char *, const char *);
void test_composition(const char *, const char *);
void test_json_composition(const char *, const char *, const char *);
namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL weather card: %s\n", message);
        std::exit(1);
    }
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "invalid data or extent must be rejected");
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 6)
        return 2;
    constexpr std::size_t bytes = 240 * 240 * 3, guard = 64;
    std::array<unsigned char, bytes + 2 * guard> storage;
    storage.fill(0xa5);
    auto pixels = std::span(storage).subspan(guard, bytes);
    ui::Surface surface(240, 240, pixels);
    weather_example::WeatherCard card(argv[1], argv[2], argv[3], argv[4]);
    const auto allocations = ui_test::memory().allocations;
    card.paint(surface);
    check(ui_test::memory().allocations == allocations, "drawing must not allocate after preparation");
    auto original = storage;
    card.paint(surface);
    check(original == storage, "repainting the same data must be deterministic");
    check(std::all_of(storage.begin(), storage.begin() + guard, [](auto b) { return b == 0xa5; }) &&
              std::all_of(storage.end() - guard, storage.end(), [](auto b) { return b == 0xa5; }),
          "RGB guards intact");
    check(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0, "rounded outer corner");
    card.paint(surface, {13, 11, 5, 0, "Deszcz"});
    check(storage != original, "description comes from a named text binding");
    check(std::equal(storage.begin(), storage.begin() + guard + 135 * 240 * 3, original.begin()),
          "description update leaves the hero intact");
    card.paint(surface, {-8, -12, 5, 0});
    check(storage != original, "temperature update changes the frame");
    check(std::equal(storage.begin() + guard + 190 * 240 * 3, storage.end() - guard,
                     original.begin() + guard + 190 * 240 * 3),
          "temperature changes leave footer intact");
    card.paint(surface, {13, 11, 12.5, 2.4});
    check(std::equal(storage.begin(), storage.begin() + guard + 190 * 240 * 3, original.begin()),
          "wind and precipitation changes leave hero intact");
    storage.fill(0x56);
    surface.set_clip({20, 200, 70, 30});
    card.paint(surface);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 240; ++x)
            if (x < 20 || x >= 90 || y < 200 || y >= 230) {
                const auto offset = (y * 240 + x) * 3;
                check(pixels[offset] == 0x56 && pixels[offset + 1] == 0x56 && pixels[offset + 2] == 0x56,
                      "respect caller clip");
            }
    surface.set_clip({0, 0, 240, 240});
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (auto bad : std::array<weather_example::Data, 5>{
             {{101, 11, 5, 0}, {13, -151, 5, 0}, {13, 11, -1, 0}, {13, 11, 5, -1}, {13, 11, nan, 0}}})
        rejects([&] { card.paint(surface, bad); });
    ui::Surface wrong(240, 239, pixels.first(240 * 239 * 3));
    rejects([&] { card.paint(wrong); });
    card.paint(surface);
    const auto prior = storage;
    rejects([&] { card.set_svg("<svg><script/></svg>"); });
    card.paint(surface);
    check(storage == prior, "failed SVG update keeps the previous cached illustration");
    test_svg();
    test_weather_svg_scenes(argv[1], argv[2], argv[3], argv[4], argv[5]);
    test_composition(argv[1], argv[2]);
    test_json_composition(argv[1], argv[2], argv[3]);
    std::puts("PASS weather 240x240: deterministic RGB, guards, clipping, model updates, validation and "
              "allocation-free paint");
}
