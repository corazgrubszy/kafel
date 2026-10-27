#include "dash5/unix.hpp"
#include "dashboard/dashboard_view.hpp"
#include "dashboard/service_feed.hpp"
#include <cstdio>
#include <fstream>
#include <thread>
#include <type_traits>
#include <unistd.h>
void check(bool, const char *);
namespace {
template <class Change> kafel::ServiceCard changed(const kafel::ServiceCard &card, Change change) {
    auto fields = card.data();
    change(fields);
    return kafel::ServiceCard(std::move(fields));
}
std::string read(const char *p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
} // namespace
void test_dashboard() {
    using namespace kafel;
    using namespace kafel::dash5;
    static_assert(!std::is_default_constructible_v<ServiceCard>);
    static_assert(
        std::is_same_v<decltype(std::declval<const ServiceCard &>().data()), const ServiceCard::Fields &>);
    for (auto state : {TileState::Ok, TileState::Warning}) {
        bool rejected = false;
        try {
            ServiceCard invalid({.owner = "test",
                                 .id = "one",
                                 .title = "Test",
                                 .state = state,
                                 .message = {},
                                 .attention = false,
                                 .offline = false,
                                 .frame = {}});
        } catch (const std::invalid_argument &error) {
            rejected = std::string_view(error.what()) == "invalid service card";
        }
        check(rejected, "healthy/warning card cannot exist online without a prepared frame");
    }
    const ServiceCard offline({.owner = "test",
                               .id = "one",
                               .title = "Test",
                               .state = TileState::Ok,
                               .message = {},
                               .attention = false,
                               .offline = true,
                               .frame = {}});
    auto recovery = offline.data();
    recovery.offline = false;
    bool refused = false;
    try {
        ServiceCard invalid(std::move(recovery));
    } catch (const std::invalid_argument &error) {
        refused = std::string_view(error.what()) == "invalid service card";
    }
    check(refused && offline.offline(), "invalid recovery cannot mutate an accepted card");
    DashboardView view;
    SystemSnapshot system;
    system.hostname.assign("kafel");
    system.ipv4.assign("192.0.2.220");
    system.cpu_percent = 37;
    system.ram_percent = 42;
    system.storage_percent = 18;
    system.storage_emmc = true;
    view.update_system(system);
    view.update_clock({16, 35}, 0);
    view.update_date(2026, 10, 17, 6);
    std::array<Pixel, 240 * 320> pixels{};
    Rgb565Surface output(pixels, 240, 320, 240);
    check(view.render(output, 0), "system page render");
    const auto system_frame = pixels;
    std::vector<unsigned char> red_pixels(240 * 240 * 3);
    for (std::size_t i = 0; i < red_pixels.size(); i += 3)
        red_pixels[i] = 255;
    auto red =
        std::make_shared<ui::CardFrame>(std::move(red_pixels), 0, std::vector<ui::MarqueeLayer>{}, 0, 4);
    std::array<ServiceCard, 2> cards = {
        {ServiceCard({"weather", "one", "Pogoda", kafel::dash5::TileState::Ok, "", false, false, red}),
         ServiceCard({"waste", "two", "Śmieci", kafel::dash5::TileState::Ok, "", false, false, red})}};
    static_assert(std::is_nothrow_move_constructible_v<ServiceCard>);
    static_assert(std::is_nothrow_move_assignable_v<ServiceCard>);
    {
        ServiceCard source = cards[0];
        ServiceCard destination(std::move(source));
        check(destination == cards[0] && destination.has_content(),
              "move construction transfers the entire checked card");
        // Do not inspect the moved-from source: restore it by assignment.
        source = cards[1];
        check(source == cards[1], "copy assignment restores a moved-from card");
        destination = std::move(source);
        check(destination == cards[1] && destination.has_content(),
              "move assignment transfers the entire checked card");
        source = ServiceCard(cards[0].data());
        check(source == cards[0], "move assignment restores a moved-from card");
        const std::array<ServiceCard, 2> restored = {source, destination};
        check(view.replace_services(restored, 0), "restored cards can be published again");
    }
    check(view.replace_services(cards, 0), "two large service pages");
    check(!view.has_attention(), "ordinary pages have no synthetic attention");
    using K = TouchSample::Kind;
    view.pointer({K::Down, 210, 100}, 100);
    view.pointer({K::Move, 10, 100}, 500);
    view.pointer({K::Up, 10, 100}, 510);
    view.render(output, 1200);
    check(view.selected_tile() == 1 && view.ribbon().offset(1200) == 240,
          "large card gesture snaps one page");
    check(pixels[120 * 240 + 120] == rgb(255, 0, 0), "service graphic fills main page");
    for (std::size_t i = 240 * 240; i < pixels.size(); ++i)
        check(pixels[i] == system_frame[i], "CPU RAM eMMC unchanged by page navigation");
    view.render(output, 1250);
    check(!view.needs_redraw(1300), "static service page sleeps");
    view.pointer({K::Down, 210, 280}, 1400);
    view.pointer({K::Move, 10, 280}, 1500);
    view.pointer({K::Up, 10, 280}, 1510);
    view.render(output, 2200);
    check(view.selected_tile() == 1, "bottom metrics do not steal pager gesture");
    std::swap(cards[0], cards[1]);
    check(view.replace_services(cards, 2300) && view.selected_tile() == 2,
          "reorder preserves service/tile identity");
    view.update(62300);
    view.render(output, 63000);
    check(view.selected_tile() == 0, "idle returns to system page");
    {
        std::vector<ui::MarqueeLayer> layers{{{12, 208, 216, 20},
                                              12,
                                              210,
                                              240,
                                              16,
                                              24,
                                              {255, 255, 255},
                                              std::vector<unsigned char>(240 * 16, 255)}};
        auto moving = std::make_shared<ui::CardFrame>(red->rgb(), 0, std::move(layers), 0, 4);
        DashboardView scrolling_view;
        const std::array<ServiceCard, 1> moving_cards = {
            {ServiceCard({"waste", "one", "Śmieci", kafel::dash5::TileState::Ok, "", false, false, moving})}};
        scrolling_view.replace_services(moving_cards, 0);
        scrolling_view.render(output, 0);
        check(!scrolling_view.next_visual_change(100) && !scrolling_view.needs_redraw(100),
              "hidden marquee never wakes or redraws system page");
        scrolling_view.pointer({K::Down, 210, 100}, 100);
        scrolling_view.pointer({K::Move, 10, 100}, 500);
        scrolling_view.pointer({K::Up, 10, 100}, 510);
        scrolling_view.render(output, 1200);
        check(!scrolling_view.needs_redraw(2400), "visible marquee sleeps throughout initial pause");
        check(scrolling_view.next_visual_change(1200) == 2442,
              "poll wakes at first actual text pixel change");
        check(scrolling_view.needs_redraw(2442), "marquee dirties view at movement deadline");
        scrolling_view.render(output, 3400);
        check(!scrolling_view.needs_redraw(4500), "end pause does not request framebuffer writes");
        check(scrolling_view.next_visual_change(3400) == 4642, "return starts after end pause");
        scrolling_view.render(output, 61000);
        scrolling_view.render(output, 62000);
        check(!scrolling_view.next_visual_change(62000) && !scrolling_view.needs_redraw(62000),
              "idle return hides and stops marquee scheduling");
    }
    {
        DashboardView health;
        check(health.service_health() == DashboardView::ServiceHealth::Empty, "no services means BRAK");
        auto states = cards;
        for (auto &card : states)
            card = changed(card, [](auto &f) { f.attention = false; });
        for (auto &card : states)
            card = changed(card, [](auto &f) { f.state = kafel::dash5::TileState::Ok; });
        health.replace_services(states, 0);
        health.render(output, 0);
        check(health.service_health() == DashboardView::ServiceHealth::Ok, "all services healthy means OK");
        states[1] = changed(states[1], [](auto &f) { f.state = kafel::dash5::TileState::Warning; });
        health.replace_services(states, 10);
        check(health.service_health() == DashboardView::ServiceHealth::Warning && health.needs_redraw(10),
              "warning means UWAGA and redraws system");
        health.render(output, 10);
        states[1] = changed(states[1], [](auto &f) { f.state = kafel::dash5::TileState::Error; });
        health.replace_services(states, 20);
        check(health.service_health() == DashboardView::ServiceHealth::Error && health.needs_redraw(20),
              "warning to error redraws status even with unchanged attention");
        states[1] = changed(states[1], [](auto &f) { f.state = kafel::dash5::TileState::Ok; });
        states[1] = changed(states[1], [](auto &f) { f.offline = true; });
        health.replace_services(states, 30);
        check(health.service_health() == DashboardView::ServiceHealth::Error, "offline service means BŁĄD");
        states[1] = changed(states[1], [](auto &f) { f.offline = false; });
        states[1] = changed(states[1], [](auto &f) { f.attention = true; });
        health.replace_services(states, 40);
        check(health.service_health() == DashboardView::ServiceHealth::Ok && health.has_attention(),
              "pickup attention does not become a service failure");
        health.replace_services({}, 50);
        check(health.service_health() == DashboardView::ServiceHealth::Empty,
              "last service removal restores BRAK");
    }
    {
        // Attention is data, not permission to draw stars or reroute a swipe.
        auto normal_cards = cards;
        for (auto &c : normal_cards) {
            c = changed(c, [](auto &f) {
                f.state = kafel::dash5::TileState::Ok;
                f.attention = false;
            });
        }
        auto attention_cards = normal_cards;
        attention_cards[1] = changed(attention_cards[1], [](auto &f) { f.attention = true; });
        DashboardView normal, important;
        normal.replace_services(normal_cards, 0);
        important.replace_services(attention_cards, 0);
        normal.render(output, 0);
        const auto normal_system = pixels;
        important.render(output, 0);
        check(pixels == normal_system, "attention flag cannot draw stray glyph before system OK");
        auto gesture = [&](DashboardView &v, bool forward, std::uint64_t start, unsigned duration) {
            v.pointer({K::Down, forward ? 210 : 10, 100}, start);
            v.pointer({K::Move, forward ? 10 : 210, 100}, start + duration);
            v.pointer({K::Up, forward ? 10 : 210, 100}, start + duration + 1);
            v.render(output, start + duration + 650);
        };
        for (unsigned duration : {20u, 100u, 400u}) {
            DashboardView pager;
            pager.replace_services(attention_cards, 0);
            gesture(pager, true, 100, duration);
            check(pager.selected_tile() == 1,
                  "one swipe opens weather even when later waste needs attention");
            gesture(pager, true, 1000, duration);
            check(pager.selected_tile() == 2, "second swipe opens waste");
            gesture(pager, false, 2000, duration);
            check(pager.selected_tile() == 1, "fast reverse swipe visits adjacent weather page");
            gesture(pager, false, 3000, duration);
            check(pager.selected_tile() == 0, "next reverse swipe returns to clock");
        }
        for (auto state : {kafel::dash5::TileState::Warning, kafel::dash5::TileState::Error}) {
            auto faulty = normal_cards;
            faulty[1] = changed(faulty[1], [&](auto &f) { f.state = state; });
            DashboardView pager;
            pager.replace_services(faulty, 0);
            gesture(pager, true, 100, 20);
            check(pager.selected_tile() == 1, "later warning or error cannot hijack navigation");
        }
        gesture(normal, true, 100, 100);
        gesture(normal, true, 1000, 100);
        const auto normal_waste = pixels;
        gesture(important, true, 100, 100);
        gesture(important, true, 1000, 100);
        check(pixels == normal_waste, "attention cannot add stray glyph on service card");
        for (int travel : {4, 12, 23}) {
            DashboardView jitter;
            jitter.replace_services(attention_cards, 0);
            jitter.pointer({K::Down, 120, 100}, 100);
            jitter.pointer({K::Move, 120 - travel, 100}, 101);
            jitter.pointer({K::Up, 120 - travel, 100}, 102);
            jitter.render(output, 800);
            check(jitter.selected_tile() == 0, "small fast finger movement does not fling to another page");
        }
    }
    // Real native transport + independent preparation worker + approved font/SVG renderer.
    char directory[] = "/tmp/kafel-dash5.XXXXXX";
    check(mkdtemp(directory) != nullptr, "private temporary directory");
    auto path = std::string(directory) + "/dashboard.sock";
    {
        ServiceFeed feed(path);
        auto tpl = read("docs/dash5/weather/template.json"),
             svg = read("docs/dash5/weather/illustration.svg");
        auto animated_template = parse(tpl);
        for (auto &t : animated_template.get<Object>()["texts"].get<Array>())
            if (t.get("content").is<Object>() && t.get("content").get("field") == Json("description"))
                t.get<Object>()["overflow"] = Json("marquee");
        animated_template.get<Object>()["texts"].get<Array>().push_back(parse(
            R"({"at":[14,114],"clip":[14,112,212,26],"style":"title","content":{"field":"description","format":"text"},"overflow":"marquee"})"));
        tpl = animated_template.serialize();
        Resources resources;
        for (auto pair :
             {std::pair{tpl, "application/vnd.kafel.tile+json"}, std::pair{svg, "image/svg+xml"}}) {
            auto id = resource_id(pair.first);
            resources[id] = std::make_shared<Resource>(Resource{id, pair.second, pair.first});
        }
        Publisher publisher(path, "kafel-weather", "Pogoda", resources);
        auto message = parse(read("docs/dash5/weather/publish.json"));
        message.get<Object>()["tiles"].get<Array>()[0].get<Object>()["content"].get<Object>()["template"] =
            Json(resource_id(tpl));
        auto tiles = message.get("tiles").serialize();
        auto missing = message.get("tiles").get<Array>()[0];
        missing.get<Object>()["id"] = Json("missing");
        missing.get<Object>()["title"] = Json("Awaria");
        missing.get<Object>()["state"] = Json("error");
        missing.get<Object>()["freshness"] = Json("missing");
        missing.get<Object>()["message"] = Json("Brak danych");
        missing.get<Object>()["observed_at"] = Json();
        missing.get<Object>()["content"] = Json();
        const auto healthy = message.get("tiles").get<Array>()[0];
        check(publisher.publish(Json(Array{missing, healthy}).serialize()) == 0,
              "cold-cache snapshot with missing tile before healthy tile is accepted");
        const auto mixed = feed.consume();
        check(mixed->size() == 2 && !mixed->at(0).frame() && mixed->at(1).frame(),
              "missing and healthy frames keep their independent positions without null dereference");
        check(publisher.publish(Json(Array{missing}).serialize()) == 0 && !feed.consume()->at(0).frame(),
              "all-missing snapshot remains valid and bounded");
        int code = publisher.publish(tiles);
        if (code)
            std::fprintf(stderr, "native publish error=%d %s\n", code, publisher.error_message().c_str());
        check(code == 0, "real publish with cold resource handshake and prepared Sans/SVG");
        auto actual = feed.consume();
        check(actual->size() == 1 && actual->at(0).frame() &&
                  actual->at(0).frame()->rgb().size() == 240 * 240 * 3,
              "worker publishes immutable real frame");
        auto frame = actual->at(0).frame();
        check(frame->glyph_bytes() < 1024 * 1024, "weather prepares only used glyphs, below 1 MiB");
        std::printf("Weather transient glyph masks: %zu bytes\n", frame->glyph_bytes());
        const auto before_cost = feed.preparation_costs();
        check(before_cost.templates == 1,
              "resource validation and subsequent publications compile one shared template");
        check(publisher.heartbeat() == 0, "real heartbeat ACK without rebuilding a packet");
        const auto heartbeat_cost = feed.preparation_costs();
        check(heartbeat_cost.templates == before_cost.templates &&
                  heartbeat_cost.frames == before_cost.frames && heartbeat_cost.fonts == before_cost.fonts &&
                  heartbeat_cost.svg == before_cost.svg,
              "heartbeat performs no template/font/SVG/frame preparation");
        check(feed.consume() == actual, "heartbeat does not invalidate UI or allocate another frame");
        auto same_pixels = message.get("tiles");
        same_pixels.get<Array>()[0]
            .get<Object>()["content"]
            .get<Object>()["data"]
            .get<Object>()["temperature"] = Json(13.1);
        check(publisher.publish(same_pixels.serialize()) == 0, "new raw measurement accepted");
        check(feed.consume()->at(0).frame() == frame,
              "unchanged formatted pixels reuse frame identity and do not dirty UI");

        const auto unchanged_cost = feed.preparation_costs();
        check(unchanged_cost.templates == before_cost.templates &&
                  unchanged_cost.frames == before_cost.frames && unchanged_cost.fonts == before_cost.fonts &&
                  unchanged_cost.svg == before_cost.svg,
              "invisible raw change performs zero template/font/SVG/frame preparation");
        auto changed_temperature = same_pixels;
        changed_temperature.get<Array>()[0]
            .get<Object>()["content"]
            .get<Object>()["data"]
            .get<Object>()["temperature"] = Json(14.1);
        check(publisher.publish(changed_temperature) == 0, "visible temperature update accepted");
        const auto updated_cost = feed.preparation_costs();
        check(updated_cost.templates == before_cost.templates && updated_cost.svg == before_cost.svg &&
                  updated_cost.frames == before_cost.frames + 1 &&
                  updated_cost.fonts <= before_cost.fonts + 1,
              "temperature update reuses compiled template, caption font and rasterized SVG");
        std::printf("Update cost: invisible=0 frames/0 fonts/0 SVG; visible=1 frame/%zu fonts/0 SVG\n",
                    updated_cost.fonts - before_cost.fonts);

        auto long_caption = same_pixels;
        long_caption.get<Array>()[0]
            .get<Object>()["content"]
            .get<Object>()["data"]
            .get<Object>()["description"] = Json("Zachmurzenie zachmurzenie zachmurzenie");
        check(publisher.publish(long_caption.serialize()) == 0, "long dynamic caption accepted");
        const auto first_scroll = feed.consume()->at(0).frame();
        long_caption.get<Array>()[0]
            .get<Object>()["content"]
            .get<Object>()["data"]
            .get<Object>()["description"] = Json("Zachmurzenie zachmurzenie zachmurzenia");
        check(publisher.publish(long_caption.serialize()) == 0, "changed dynamic caption accepted");
        const auto second_scroll = feed.consume()->at(0).frame();
        check(first_scroll->rgb() == second_scroll->rgb() && first_scroll != second_scroll &&
                  first_scroll->marquees() != second_scroll->marquees(),
              "identical static background cannot hide changed marquee content in frame cache");
        view.replace_services(*actual, 64000);
        view.pointer({K::Down, 210, 100}, 64100);
        view.pointer({K::Move, 10, 100}, 64500);
        view.pointer({K::Up, 10, 100}, 64510);
        view.render(output, 65200);
        std::ofstream screenshot("/tmp/kafel-dash5-weather.ppm", std::ios::binary);
        screenshot << "P6\n240 320\n255\n";
        for (auto p : pixels) {
            char bytes[] = {char(((p >> 11) & 31) * 255 / 31), char(((p >> 5) & 63) * 255 / 63),
                            char((p & 31) * 255 / 31)};
            screenshot.write(bytes, 3);
        }
    }
    unlink((std::string(directory) + "/lock").c_str());
    rmdir(directory);
    std::puts("PASS DASH/5 large pages, physical gestures, stable metrics, identity, idle, real transport, "
              "SVG/TTF preparation and idle heartbeat");
}
