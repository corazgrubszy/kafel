#include "dashboard/clock_painter.hpp"
#include "dashboard/theme.hpp"
#include "memory.hpp"
#include "ui/grid.hpp"
#include "ui/ribbon.hpp"
#include "ui/tile.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
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
    check(rejected, "invalid input must be rejected");
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    ui::Grid grid({4, 240, 232, 76}, 3, 4);
    check(*grid.cell(0) == ui::PixelRect{4, 240, 74, 76}, "first cell");
    check(*grid.cell(1) == ui::PixelRect{82, 240, 75, 76}, "second cell");
    check(*grid.cell(2) == ui::PixelRect{161, 240, 75, 76}, "third cell");
    check(*grid.cell(0, 2) == ui::PixelRect{4, 240, 153, 76}, "two columns include gap");
    check(*grid.cell(1, 2) == ui::PixelRect{82, 240, 154, 76}, "rounded two-column width");
    check(*grid.cell(0, 3) == ui::PixelRect{4, 240, 232, 76}, "three columns");
    check(!grid.cell(-1) && !grid.cell(2, 2) && !grid.cell(0, 0) && !grid.cell(0, 4), "invalid spans");
    for (int width = 20; width < 401; ++width) {
        ui::Grid g({7, 11, width, 30}, 3, 4);
        check(g.cell(0)->x == 7 && g.cell(2)->right() == 7 + width, "grid covers bounds exactly");
        check(g.cell(1)->x - g.cell(0)->right() == 4 && g.cell(2)->x - g.cell(1)->right() == 4,
              "gap invariant");
    }
    rejects([] { ui::Grid invalid({0, 0, 10, 10}, 3, 4); });
    rejects([] { ui::Grid invalid({0, 0, 10, 10}, std::numeric_limits<int>::min(), 4); });
    ui::Grid edge({std::numeric_limits<int>::max() - 232, 0, 232, 76}, 3, 4);
    check(edge.cell(2)->right() == std::numeric_limits<int>::max(), "safe edge arithmetic");
    check(ui::TilePainter::filled_segments(0, 10) == 0 && ui::TilePainter::filled_segments(1, 10) == 1 &&
              ui::TilePainter::filled_segments(42, 10) == 5 &&
              ui::TilePainter::filled_segments(100, 10) == 10,
          "meter coverage");
    {
        ui::RibbonLayout pages, one_page;
        const std::array spans = {3u, 3u, 3u};
        const std::array single = {3u};
        check(pages.configure({0, 0, 240, 240}, spans, 0) && one_page.configure({0, 0, 240, 240}, single, 0),
              "page layouts");
        ui::RibbonController pager(60000, ui::RibbonController::Paging::Adjacent);
        pager.configure(pages, 0);
        pager.select(2, 0, false);
        pager.pointer(ui::RibbonController::Event::Down, 210, 100, 10);
        pager.pointer(ui::RibbonController::Event::Move, 10, 100, 20);
        pager.configure(one_page, 25);
        pager.pointer(ui::RibbonController::Event::Up, 10, 100, 30);
        check(pager.selected() == 0 && pager.offset(1000) == 0,
              "adjacent bounds stay valid if pages disappear during drag");
        pager.configure(pages, 1100);
        pager.select(0, 1100, false);
        pager.pointer(ui::RibbonController::Event::Down, 210, 100, 1200);
        pager.pointer(ui::RibbonController::Event::Move, 10, 100, 1220);
        pager.pointer(ui::RibbonController::Event::Up, 10, 100, 1221);
        const auto origin = static_cast<std::size_t>(std::lround(pager.offset(1300) / 240));
        pager.pointer(ui::RibbonController::Event::Down, 210, 100, 1300);
        pager.pointer(ui::RibbonController::Event::Move, 10, 100, 1320);
        pager.pointer(ui::RibbonController::Event::Up, 10, 100, 1321);
        check(pager.selected() <= origin + 1, "capturing a transition uses the nearest visible page");
    }
    const auto &theme = kafel::rose_pine_moon;
    check(theme.background == ui::Color::hex(0x232136) &&
              theme.clock_number.color == ui::Color::hex(0xc4a7e7),
          "palette roles");
    const std::array styles = {theme.eyebrow, theme.hostname, theme.date, theme.label, theme.unit};
    const std::array number_styles = {theme.clock_number, theme.metric_number, theme.metric_number_compact};
    ui::TextFont font(argv[1], "TIME / LOCALcm4stackPIĄTEK 16 PAŹ 2026CPU RAM eMMC %0123456789", styles);
    std::ifstream file(argv[2], std::ios::binary);
    std::vector<unsigned char> bold{std::istreambuf_iterator<char>(file), {}};
    ui::NumberFont numbers(bold, number_styles);
    ui::TextFont headings(bold, "cm4stack", std::array{theme.hostname});
    ui::TilePainter painter(theme.metric_style(), font, numbers);
    kafel::ClockPainter clock(theme, font, headings, numbers);
    ui::TextFont numeric_face(argv[2], "0123456789:", number_styles);
    for (char digit = '0'; digit <= '9'; ++digit) {
        const auto one = std::string_view(&digit, 1);
        check(std::abs(numeric_face.measure(one, theme.clock_number) -
                       numeric_face.measure("0", theme.clock_number)) < .001f,
              "Plex Sans has native tabular digits");
        check(numbers.advance(digit, theme.clock_number) == numbers.advance('0', theme.clock_number),
              "fixed numeric cells");
        std::array<unsigned char, 100 * 100 * 3> actual{}, expected{};
        ui::Surface tabular(100, 100, actual), ttf(100, 100, expected);
        numbers.draw(tabular, {0, 0, 100, 100}, 2, 4, one, theme.clock_number);
        numeric_face.draw(ttf, {0, 0, 100, 100}, 2,
                          4 - numeric_face.ink_bounds("0123456789:", theme.clock_number).y, one,
                          theme.clock_number);
        check(actual == expected, "numeric adapter draws actual TrueType outlines");
    }
    check(numbers.measure("11:11", theme.clock_number) == numbers.measure("88:88", theme.clock_number),
          "clock width is stable");
    check(numbers.measure("23:59", theme.clock_number) <= 212, "clock fits the existing panel");

    std::array<unsigned char, 240 * 320 * 3> pixels{};
    ui::Surface surface(240, 320, pixels);
    // Guard renderer clipping with a destination inset inside a larger buffer.
    std::array<unsigned char, 3 * 4 * 4 + 2> guarded{};
    guarded.front() = 17;
    guarded.back() = 29;
    ui::Surface tiny(4, 4, std::span<unsigned char>(guarded).subspan(1, 48));
    tiny.rectangle({-100, -100, 200, 200}, {255, 0, 0});
    tiny.blend(-1, 0, {0, 0, 0}, 255);
    check(guarded.front() == 17 && guarded.back() == 29, "surface clipping guard");
    surface.clear(theme.background);
    clock.paint(surface, {4, 4, 232, 232}, {"cm4stack", "16:35", "PIĄTEK 16 PAŹ 2026"});
    for (unsigned value : {0u, 1u, 9u, 10u, 99u, 100u})
        painter.metric(surface, *grid.cell(0), {"CPU", value});
    const auto before = ui_test::memory();
    for (int frame = 0; frame < 10; ++frame) {
        surface.clear(theme.background);
        clock.paint(surface, {4, 4, 232, 232}, {"cm4stack", "23:59", "PIĄTEK 16 PAŹ 2026"});
        for (int column = 0; column < 3; ++column)
            painter.metric(surface, *grid.cell(column), {"RAM", 42});
    }
    check(ui_test::memory().allocations == before.allocations, "no drawing allocations");
    rejects([&] { painter.metric(surface, *grid.cell(0), {"CPU", 101}); });
    rejects([&] { clock.paint(surface, {4, 4, 232, 232}, {"cm4stack", "24:00", ""}); });
    rejects([&] { font.measure("\xc0\xaf", theme.label); });
    rejects([&] { font.measure("\xf0\x9f\x8c\x9f", theme.label); });

    auto clock_frame = [&](std::string_view current, std::string_view previous, float progress) {
        surface.clear(theme.background);
        clock.paint(surface, {4, 4, 232, 232},
                    {"cm4stack", current, "PIĄTEK 16 PAŹ 2026", previous, progress});
        return pixels;
    };
    const auto old_time = clock_frame("12:35", {}, 1);
    check(clock_frame("12:36", "12:35", 0) == old_time, "animation begins exactly at old frame");
    const auto new_time = clock_frame("12:36", {}, 1);
    check(clock_frame("12:36", "12:35", 1) == new_time, "animation ends exactly at new frame");
    const auto middle = clock_frame("12:36", "12:35", .5f);
    check(middle != old_time && middle != new_time, "sliding has a distinct intermediate frame");
    const int height = numbers.height(theme.clock_number), top = 192 - numbers.baseline(theme.clock_number);
    const int changing_x = static_cast<int>(std::lround(14 + numbers.measure("12:3", theme.clock_number)));
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 240; ++x)
            if (x < changing_x || x >= changing_x + std::ceil(numbers.advance('6', theme.clock_number)) ||
                y < top || y >= top + height)
                for (int c = 0; c < 3; ++c)
                    check(middle[(y * 240 + x) * 3 + c] == old_time[(y * 240 + x) * 3 + c],
                          "only the changing digit moves; neighbours and date remain still");
    const auto before_midnight = clock_frame("23:59", {}, 1);
    check(clock_frame("00:00", "23:59", 0) == before_midnight, "midnight phase zero");
    const auto midnight = clock_frame("00:00", {}, 1);
    check(clock_frame("00:00", "23:59", 1) == midnight, "midnight settles exactly");
    const auto allocations = ui_test::memory().allocations;
    for (unsigned i = 0; i <= 10; ++i)
        (void)clock_frame("00:00", "23:59", i / 10.f);
    check(ui_test::memory().allocations == allocations, "TrueType clock animation allocates nothing");
    std::puts("PASS TrueType tabular clock, exact raster/animation endpoints, midnight, clipping and zero "
              "animation allocations");
    const auto old = pixels;
    painter.metric(surface, *grid.cell(0), {"CPU", 37});
    check(pixels != old, "model updates pixels");
    // The neighbour and all gaps must survive a single tile update.
    const auto bounds = *grid.cell(0);
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 240; ++x)
            if (x < bounds.x || x >= bounds.right() || y < bounds.y || y >= bounds.bottom())
                for (int channel = 0; channel < 3; ++channel)
                    check(pixels[(y * 240 + x) * 3 + channel] == old[(y * 240 + x) * 3 + channel],
                          "tile clips its output");
    std::puts("PASS: grid rounding/spans/gaps, palette, clipping, UTF-8, ranges, model updates, zero drawing "
              "allocations");
}
