#include "apps/waste/src/packet.hpp"
#include "apps/weather/src/packet.hpp"
#include "dashboard/dashboard_view.hpp"
#include "ui/card.hpp"
#include <array>
#include <cstdio>
#include <string>
#include <tuple>
namespace {
using namespace kafel;
extern "C" const unsigned char *ui_sans_regular_data();
extern "C" std::size_t ui_sans_regular_size();
extern "C" const unsigned char *ui_sans_bold_data();
extern "C" std::size_t ui_sans_bold_size();
std::vector<ServiceCard> cards(std::string_view scene = "system") {
    using namespace dash5;
    weather::WeatherSnapshot forecast{
        {weather::Conditions{13, 18, weather::Conditions::Kind::Clear, 11, 0, 1800000000, 2, true}}, false};
    const auto date = *waste::parse_date("2026-10-17");
    const waste::Schedule schedule{"UTC",
                                   {{date, scene == "waste-many" ? 63u : 5u},
                                    {date + std::chrono::days(1), 6u},
                                    {date + std::chrono::days(3), 16u}}};
    const auto today = scene == "waste-next"       ? date - std::chrono::days(2)
                       : scene == "waste-tomorrow" ? date - std::chrono::days(1)
                                                   : date;
    std::vector<ServiceCard> result;
    for (auto tuple :
         {std::tuple{"kafel-weather", weather::dashboard_tiles(forecast), &weather::dashboard_assets()},
          std::tuple{"kafel-waste", waste::dashboard_tiles(&schedule, today), &waste::dashboard_assets()}}) {
        auto parsed = parse(std::get<1>(tuple));
        const auto &tile = parsed.get<Array>()[0];
        const auto &bundle = *std::get<2>(tuple);
        auto content = tile.get("content");
        auto frame = ui::prepare_card(
            bundle.resources.at(content.get("template").get<std::string>())->bytes, content.get("data"),
            [&](std::string_view id) -> std::string_view {
                return bundle.resources.at(std::string(id))->bytes;
            },
            [](std::string_view name) -> std::span<const unsigned char> {
                if (name.ends_with("/bold"))
                    return {ui_sans_bold_data(), ui_sans_bold_size()};
                return {ui_sans_regular_data(), ui_sans_regular_size()};
            },
            8388608, 4);
        result.push_back(ServiceCard({.owner = std::get<0>(tuple),
                                      .id = tile.get("id").get<std::string>(),
                                      .title = tile.get("title").get<std::string>(),
                                      .state = TileState::Ok,
                                      .message = {},
                                      .attention = false,
                                      .offline = false,
                                      .frame = std::move(frame)}));
    }
    if (scene == "error" || scene == "offline" || scene == "warning") {
        auto weather = result.front().data();
        weather.state = scene == "error"     ? dash5::TileState::Error
                        : scene == "warning" ? dash5::TileState::Warning
                                             : dash5::TileState::Ok;
        weather.message = scene == "error" ? "Brak danych" : scene == "warning" ? "Nieaktualne dane" : "";
        weather.offline = scene == "offline";
        if (scene == "error")
            weather.frame.reset();
        result.front() = ServiceCard(std::move(weather));
    }
    return result;
}
void setup(DashboardView &view, std::string_view scene = "system") {
    SystemSnapshot system;
    system.hostname.assign("kafel-demo");
    system.ipv4.assign("192.0.2.220");
    system.cpu_percent = 37;
    system.ram_percent = 42;
    system.storage_percent = 18;
    system.storage_emmc = true;
    view.update_system(system);
    view.update_clock({16, 35}, 0);
    view.update_date(2026, 10, 17, 6);
    view.replace_services(cards(scene), 0);
}
bool write(const std::string &path, const std::array<Pixel, 240 * 320> &pixels) {
    auto *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fputs("P6\n240 320\n255\n", f) >= 0;
    for (auto p : pixels) {
        unsigned char rgb[] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                               static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
                               static_cast<unsigned char>((p & 31) * 255 / 31)};
        ok &= std::fwrite(rgb, 1, 3, f) == 3;
    }
    return std::fclose(f) == 0 && ok;
}
void swipe(DashboardView &view, unsigned start) {
    using K = TouchSample::Kind;
    view.pointer({K::Down, 210, 100}, start);
    view.pointer({K::Move, 10, 100}, start + 400);
    view.pointer({K::Up, 10, 100}, start + 410);
    view.update(start + 1100);
}
bool screenshot(const std::string &path, std::string_view scene) {
    DashboardView view;
    setup(view, scene);
    if (scene == "limits") {
        SystemSnapshot system;
        system.hostname.assign("kafel-demo");
        system.ipv4.assign("192.0.2.220");
        system.cpu_percent = 99;
        system.ram_percent = 100;
        system.storage_percent = 100;
        system.storage_emmc = true;
        view.update_system(system);
    }
    if (scene != "system" && scene != "limits")
        swipe(view, 100);
    if (scene.starts_with("waste"))
        swipe(view, 1300);
    std::array<Pixel, 240 * 320> pixels{};
    Rgb565Surface output(pixels, 240, 320, 240);
    return view.render(output, 2500) && write(path, pixels);
}
bool animation(const std::string &directory) {
    DashboardView view;
    setup(view);
    std::array<Pixel, 240 * 320> pixels{};
    Rgb565Surface output(pixels, 240, 320, 240);
    using K = TouchSample::Kind;
    for (unsigned frame = 0; frame < 150; ++frame) {
        unsigned now = 1000 + frame * 40;
        if (frame == 25 || frame == 85)
            view.pointer({K::Down, 210, 100}, now);
        if (frame > 25 && frame <= 40)
            view.pointer({K::Move, 210 - int((frame - 25) * 200 / 15), 100}, now);
        if (frame > 85 && frame <= 100)
            view.pointer({K::Move, 210 - int((frame - 85) * 200 / 15), 100}, now);
        if (frame == 41 || frame == 101)
            view.pointer({K::Up, 10, 100}, now);
        char name[40];
        std::snprintf(name, sizeof name, "/frame-%03u.ppm", frame);
        if (!view.render(output, now) || !write(directory + name, pixels))
            return false;
    }
    return true;
}
} // namespace
bool clock_animation(const std::string &directory) {
    kafel::DashboardView view;
    setup(view);
    std::array<kafel::Pixel, 240 * 320> pixels{};
    kafel::Rgb565Surface output(pixels, 240, 320, 240);
    for (unsigned frame = 0; frame < 80; ++frame) {
        const auto now = frame * 40u;
        if (frame == 20)
            view.update_clock({16, 36}, now);
        if (frame == 50)
            view.update_clock({16, 37}, now);
        char name[48];
        std::snprintf(name, sizeof name, "/clock-%03u.ppm", frame);
        if (!view.render(output, now) || !write(directory + name, pixels))
            return false;
    }
    return true;
}
int main(int argc, char **argv) {
    try {
        if (argc < 2 || argc > 3)
            return 2;
        if (std::string_view(argv[1]) == "--clock-frames") {
            if (argc != 3)
                return 2;
            return clock_animation(argv[2]) ? 0 : 1;
        }
        if (std::string_view(argv[1]) == "--assets") {
            if (argc != 3)
                return 2;
            std::string dir = argv[2];
            return screenshot(dir + "/dashboard.ppm", "system") &&
                           screenshot(dir + "/weather.ppm", "weather") &&
                           screenshot(dir + "/waste.ppm", "waste") &&
                           screenshot(dir + "/waste-next.ppm", "waste-next") &&
                           screenshot(dir + "/waste-tomorrow.ppm", "waste-tomorrow") && animation(dir)
                       ? 0
                       : 1;
        }
        auto scene = argc == 3 ? std::string_view(argv[2]) : std::string_view("system");
        if (scene != "system" && scene != "weather" && scene != "waste" && scene != "waste-next" &&
            scene != "waste-tomorrow" && scene != "waste-many" && scene != "limits" && scene != "error" &&
            scene != "offline" && scene != "warning")
            return 2;
        return screenshot(argv[1], scene) ? 0 : 1;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "preview: %s\n", e.what());
        return 1;
    }
}
