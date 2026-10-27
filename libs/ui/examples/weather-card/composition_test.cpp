#include "memory.hpp"
#include "ui/composition.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL composition: %s\n", message);
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
    check(failed, "bad definition or binding must be rejected");
}
} // namespace
void test_composition(const char *regular_path, const char *bold_path) {
    using namespace ui;
    constexpr Color white{255, 255, 255};
    constexpr std::array styles = {TextStyle{10, white}, TextStyle{14, white}};
    TextFont regular(regular_path, "0123456789- .VRPM", styles), bold(bold_path, "0123456789- .VRPM", styles);
    const std::array<const TextFont *, 2> fonts = {&regular, &bold};
    const std::array rules = {FieldRule{"voltage", ValueFormat::General, 0, 100},
                              FieldRule{"label", ValueFormat::Text, 0, 0, 12}};
    const std::array texts = {TextElement{2, 2, {0, 0, 48, 32}, 0, styles[0], {.field = "label"}}};
    const std::array metric_styles = {styles[1]};
    const std::array metrics = {MetricElement{2,
                                              15,
                                              {2, 14, 30, 18},
                                              1,
                                              metric_styles,
                                              22,
                                              {.field = "voltage", .format = ValueFormat::General},
                                              "V",
                                              styles[1],
                                              15,
                                              2}};
    const Composition definition{
        48, 32, {{20, 25, 30}, {24, 10}, {20, 20}, {0, 0, 0}, 48, 1, 0, {0, 0, 0}, 2}, texts, metrics, rules};
    CompositionPainter painter(definition, fonts);
    std::array<unsigned char, 48 * 32 * 3> pixels{};
    Surface surface(48, 32, pixels);
    std::array values = {Field{"voltage", 24.0}, Field{"label", std::string_view("RPM")}};
    const auto allocations = ui_test::memory().allocations;
    painter.paint(surface, values);
    check(ui_test::memory().allocations == allocations, "generic composition draws without allocations");
    const auto expected = pixels;
    std::swap(values[0], values[1]);
    painter.paint(surface, values);
    check(pixels == expected, "bindings use names, independent of field order");
    std::array<unsigned char, 48 * 32 * 4> rgba{};
    const auto last = (48 * 32 - 1) * 4;
    rgba[last] = 200;
    rgba[last + 1] = 100;
    rgba[last + 2] = 50;
    rgba[last + 3] = 255;
    const RgbaImage overlay{48, 32, rgba};
    painter.paint(surface, values, &overlay);
    check(pixels[pixels.size() - 3] == 200 && pixels[pixels.size() - 2] == 100 && pixels.back() == 50,
          "generic RGBA overlay receives pixels, not illustration IDs");
    const auto with_overlay = pixels;
    const RgbaImage wrong_overlay{47, 32, rgba};
    rejects([&] { painter.paint(surface, values, &wrong_overlay); });
    check(pixels == with_overlay, "invalid RGBA leaves the old frame untouched");
    painter.paint(surface, values);
    auto unchanged = [&](auto bad) {
        rejects([&] { painter.paint(surface, bad); });
        check(pixels == expected, "bad data must leave old frame intact");
    };
    unchanged(std::span(values).first(1));
    auto bad = values;
    bad[1] = bad[0];
    unchanged(bad);
    bad = values;
    bad[1].value = std::string_view("24");
    unchanged(bad);
    bad = values;
    bad[1].value = std::numeric_limits<double>::quiet_NaN();
    unchanged(bad);
    bad = values;
    bad[1].value = 101.0;
    unchanged(bad);
    bad = values;
    bad[0].name = "unknown";
    unchanged(bad);
    bad = values;
    bad[0].value = std::string_view("too long for this field");
    unchanged(bad);
    bad = values;
    bad[0].value = std::string_view("☀");
    unchanged(bad);
    bad = values;
    bad[0].value = std::string_view("\xff");
    unchanged(bad);
    auto invalid = definition;
    invalid.width = -1;
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    invalid = definition;
    invalid.backdrop.stripe_period = 0;
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    invalid = definition;
    invalid.backdrop.glow_radius.x = 0;
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    auto invalid_texts = texts;
    invalid_texts[0].font = 2;
    invalid = definition;
    invalid.texts = invalid_texts;
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    invalid_texts = texts;
    invalid_texts[0].content.field = "unknown";
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    invalid_texts = texts;
    invalid_texts[0].clip = {40, 0, 20, 20};
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    invalid_texts = texts;
    invalid_texts[0].style.tracking = std::numeric_limits<float>::infinity();
    rejects([&] { CompositionPainter rejected(invalid, fonts); });
    // Formatting capacity is bounded even when a valid string and prefix combine.
    invalid_texts = texts;
    invalid_texts[0].content.prefix =
        std::string_view("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    CompositionPainter overflow(invalid, fonts);
    rejects([&] { overflow.paint(surface, values); });
    check(pixels == expected, "format overflow is atomic");
    // Bad glyph sizes must be caught in preflight, not after the backdrop is drawn.
    invalid_texts = texts;
    invalid_texts[0].style.em_px = 11;
    CompositionPainter unprepared(invalid, fonts);
    rejects([&] { unprepared.paint(surface, values); });
    check(pixels == expected, "glyph preflight is atomic");
    std::puts("PASS generic composition: non-weather scene, named bindings, bounds, atomic failure and "
              "allocation-free drawing");
}
