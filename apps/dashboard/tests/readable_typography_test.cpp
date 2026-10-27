#include "dashboard/dashboard_view.hpp"
#include "dashboard/large_tile_layout.hpp"
#include "dashboard/theme.hpp"
#include "system/system_metrics.hpp"
#include <cmath>
#include <cstdio>
void check(bool, const char *);
extern "C" const unsigned char *ui_sans_regular_data();
extern "C" std::size_t ui_sans_regular_size();
extern "C" const unsigned char *ui_sans_bold_data();
extern "C" std::size_t ui_sans_bold_size();
void test_readable_typography() {
    using kafel::format_calendar_date;
    const auto &theme = kafel::rose_pine_moon;
    const std::array styles = {theme.secondary,      theme.date,         theme.label,
                               theme.unit,           theme.service_name, theme.service_state,
                               theme.service_detail, theme.attention};
    ui::TextFont font({ui_sans_regular_data(), ui_sans_regular_size()},
                      "CPU RAM eMMC %Poniedziałek Wtorek Środa Czwartek Piątek Sobota Niedziela 0123456789. "
                      "sty lut mar kwi maj cze lip sie wrz paź lis gru OFFLINE AWARIA UWAGA OK",
                      styles);
    for (auto label : {"CPU", "RAM", "eMMC"})
        check(font.measure(label, theme.label) <= 58, "readable labels fit narrowest tile");
    check(font.measure("255.255.255.255", theme.secondary) <= 190,
          "longest IPv4 fits second header line at 14px");
    check(theme.hostname.em_px == 22 && theme.secondary.em_px == 14 && theme.date.em_px == 18,
          "large tile title, subdued IP and caption use shared hierarchy");
    std::array<unsigned char, 190 * 38 * 3> header{};
    ui::Surface header_surface(190, 38, header);
    header_surface.clear(theme.clock.fill);
    font.draw(header_surface, {0, 0, 190, 20}, 0, 3, "Poniedziałek", theme.secondary);
    font.draw(header_surface, {0, 18, 190, 20}, 0, 21, "255.255.255.255", theme.secondary);
    for (int y = 17; y < 20; ++y)
        for (int x = 0; x < 190; ++x)
            check(header[(y * 190 + x) * 3] == theme.clock.fill.r,
                  "14px header lines keep a clear vertical gap");
    check(theme.label.em_px == 14 && theme.unit.em_px == 14 && theme.service_name.em_px == 14 &&
              theme.service_state.em_px == 14,
          "ribbon labels, units and service headers use readable 14px text");
    const std::array number_styles = {theme.clock_number, theme.metric_number, theme.metric_number_compact};
    ui::NumberFont numeric({ui_sans_bold_data(), ui_sans_bold_size()}, number_styles);
    const float numbers = numeric.measure("99", theme.metric_number);
    check(numbers + 1 + font.measure("%", theme.unit) <= 58,
          "two-digit resource reading and unit fit at the fixed size");
    const ui::TextStyle failure_style{kafel::LargeTileLayout::failure_value_size, theme.error.accent, 0};
    ui::TextFont failure({ui_sans_bold_data(), ui_sans_bold_size()}, "AwariaOffline",
                         std::array{failure_style});
    check(failure_style.em_px == 60, "failure labels have a fixed 60px size");
    for (auto label : {"Awaria", "Offline"}) {
        const auto ink = failure.ink_bounds(label, failure_style);
        check(failure.measure(label, failure_style) <= 212 && ink.x + ink.width <= 212,
              "both mixed-case failure labels fit without shrinking or clipping");
    }
    const auto hero_style = theme.clock_number;
    ui::TextFont hero({ui_sans_bold_data(), ui_sans_bold_size()},
                      "DzisiajJutroBrak0123456789 sty lut mar kwi maj cze lip sie wrz paź lis gru",
                      std::array{hero_style});
    check(hero_style.em_px == kafel::LargeTileLayout::value_size &&
              hero.measure("Dzisiaj", hero_style) <= 212 &&
              hero.ink_bounds("Dzisiaj", hero_style).x + hero.ink_bounds("Dzisiaj", hero_style).width <=
                  212 &&
              hero.measure("Jutro", hero_style) <= 212,
          "both waste captions fit at the exact 60px clock and temperature size");
    check(hero_style.em_px == 60, "clock, temperature and waste share a fixed 60px role");
    check(kafel::LargeTileLayout::value_baseline - hero.cap_height(hero_style) == 150,
          "60px service text and clock retain the shared 192px baseline");
    for (auto month : {"sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru"})
        for (unsigned day = 1; day <= 31; ++day) {
            char date[24];
            std::snprintf(date, sizeof date, "%u %s", day, month);
            const auto ink = hero.ink_bounds(date, hero_style);
            check(hero.measure(date, hero_style) <= 212 && ink.x + ink.width <= 212,
                  "every day/month caption fits at 60px without a smaller month or adaptive fitting");
            const auto top = kafel::LargeTileLayout::value_baseline - hero.cap_height(hero_style);
            check(top + ink.y >= 134 && top + ink.y + ink.height <= 208,
                  "date clip preserves accents and descenders of every month at the shared baseline");
        }
    check(format_calendar_date(2026, 10, 16, 5)->view() == "Piątek 16 paź",
          "full Polish weekday without year");
    for (unsigned month = 1; month <= 12; ++month)
        for (unsigned day = 0; day < 7; ++day) {
            const auto formatted = format_calendar_date(9999, month, 28, day);
            check(formatted && font.measure(formatted->view(), theme.date) <= 210,
                  "all dates fit at readable size");
        }
    check(!format_calendar_date(2026, 2, 29, 0) && format_calendar_date(2028, 2, 29, 0).has_value(),
          "calendar leap validation preserved");
    std::array<unsigned char, 10 * 10 * 3> corner_pixels{};
    ui::Surface rounded(10, 10, corner_pixels);
    rounded.rounded_rectangle({0, 0, 10, 10}, 3, {255, 0, 0});
    check(corner_pixels[0] == 0 && corner_pixels[5 * 3] == 255 && corner_pixels[(5 * 10) * 3] == 255,
          "rounded panel has empty corners and no border along edges");
    check(corner_pixels[3] > 0 && corner_pixels[3] < 255, "rounded corners use partial coverage");
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            check(corner_pixels[(y * 10 + x) * 3] == corner_pixels[(y * 10 + 9 - x) * 3] &&
                      corner_pixels[(y * 10 + x) * 3] == corner_pixels[((9 - y) * 10 + x) * 3],
                  "rounded panel is symmetric");
    std::array<unsigned char, 74 * 76 * 3> pixels{};
    ui::Surface surface(74, 76, pixels);
    surface.clear(theme.metric.fill);
    font.draw(surface, {8, 10, 58, 18}, 8, 10, "CPU", theme.label);
    int rows = 0;
    for (int y = 10; y < 28; ++y) {
        bool ink = false;
        for (int x = 8; x < 66; ++x)
            ink |= pixels[(y * 74 + x) * 3] != theme.metric.fill.r;
        rows += ink;
    }
    check(rows >= 8, "metric label has at least eight visible pixel rows");
    ui::TextFont headings({ui_sans_bold_data(), ui_sans_bold_size()}, "CPU RAM eMMC cm4stack",
                          std::array{theme.hostname});
    ui::TilePainter painter(theme.metric_style(), font, numeric);
    for (const auto label : {"CPU", "RAM", "eMMC"})
        for (const auto percent :
             {std::optional<unsigned>{37}, std::optional<unsigned>{100}, std::optional<unsigned>{}}) {
            painter.metric(surface, {0, 0, 74, 76}, {label, percent});
            const auto ink = font.ink_bounds(label, theme.label);
            const auto style = theme.metric_number;
            const int meter_top = 76 - theme.metric.inset - theme.meter.height;
            const int label_bottom = theme.metric.inset + ink.height;
            const auto unit_ink = font.ink_bounds("%", theme.unit);
            const int reading_height =
                std::max(numeric.height(style), numeric.baseline(style) - font.cap_height(theme.unit) +
                                                    unit_ink.y + unit_ink.height);
            const int number_top = label_bottom + (meter_top - label_bottom - reading_height) / 2;
            const int number_bottom = number_top + reading_height;
            check(std::abs((number_top - label_bottom) - (meter_top - number_bottom)) <= 1,
                  "metric reading has balanced space above and below");
            auto background_row = [&](int y) {
                for (int x = 8; x < 66; ++x) {
                    const auto i = (y * 74 + x) * 3;
                    check(pixels[i] == theme.metric.fill.r && pixels[i + 1] == theme.metric.fill.g &&
                              pixels[i + 2] == theme.metric.fill.b,
                          "metric outer padding and internal gaps stay clear");
                }
            };
            for (int y = 0; y < theme.metric.inset; ++y)
                background_row(y);
            for (int y = label_bottom; y < number_top; ++y)
                background_row(y);
            for (int y = number_bottom; y < meter_top; ++y)
                background_row(y);
            for (int y = 76 - theme.metric.inset; y < 76; ++y)
                background_row(y);
            const auto i = (meter_top * 74 + 8) * 3;
            const auto expected = percent.value_or(0) ? theme.meter.filled : theme.meter.empty;
            check(pixels[i] == expected.r && pixels[i + 1] == expected.g && pixels[i + 2] == expected.b,
                  "metric meter retains height with equal outer padding");
        }
    std::array<unsigned char, 74 * 76 * 3> saturated{}, maximum{};
    ui::Surface saturated_surface(74, 76, saturated), maximum_surface(74, 76, maximum);
    saturated_surface.clear(theme.metric.fill);
    maximum_surface.clear(theme.metric.fill);
    painter.metric(saturated_surface, {0, 0, 74, 76}, {"CPU", 100});
    painter.metric(maximum_surface, {0, 0, 74, 76}, {"CPU", 99});
    check(saturated == maximum, "100% displays 99% at unchanged font size with a full meter");
    // The actual offline state and attention star must survive a long name.
    kafel::DashboardView view;
    kafel::SystemSnapshot system;
    system.hostname.assign("cm4stack");
    view.update_system(system);
    view.update_clock({16, 35}, 0);
    view.update_date(2026, 10, 16, 5);
    system.ipv4.assign("255.255.255.255");
    view.update_system(system);
    std::array<kafel::Pixel, 240 * 320> frame{};
    kafel::Rgb565Surface output(frame, 240, 320, 240);
    view.render(output, 0);
    const auto clock_background = kafel::rgb(theme.clock.fill.r, theme.clock.fill.g, theme.clock.fill.b);
    for (int y = 195; y < 208; ++y)
        for (int x = 14; x < 226; ++x)
            check(frame[y * 240 + x] == clock_background, "clock/date breathing room unchanged");
    std::puts("PASS readable system typography, metric bars, IPv4, calendar and clock/date separation");
}
