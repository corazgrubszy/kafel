#include "dash5/bundle.hpp"
#include "ui/card.hpp"
#include "ui/composition_json.hpp"
#include "ui/svg.hpp"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <fstream>
#include <set>
#include <type_traits>
void check(bool, const char *);
namespace {
using namespace kafel::dash5;
extern "C" const unsigned char *ui_sans_regular_data();
extern "C" std::size_t ui_sans_regular_size();
extern "C" const unsigned char *ui_sans_bold_data();
extern "C" std::size_t ui_sans_bold_size();
std::string file(const char *name) {
    std::ifstream f(name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
std::span<const unsigned char> font(std::string_view face) {
    return face.ends_with("/bold") ? std::span(ui_sans_bold_data(), ui_sans_bold_size())
                                   : std::span(ui_sans_regular_data(), ui_sans_regular_size());
}
template <class F> void rejects(F f, std::string_view expected) {
    bool rejected = false;
    try {
        f();
    } catch (const std::invalid_argument &error) {
        if (error.what() != expected)
            std::fprintf(stderr, "card error: got %s expected %.*s\n", error.what(), int(expected.size()),
                         expected.data());
        check(error.what() == expected, "expected card rejection code");
        rejected = true;
    }
    check(rejected, "invalid card/profile rejected before publication");
}
} // namespace
void test_cards() {
    using namespace kafel::dash5;
    auto pixels_at = [](const ui::CardFrame &frame, std::uint64_t elapsed) {
        std::vector<unsigned char> pixels(240 * 240 * 3);
        ui::Surface surface(240, 240, pixels);
        surface.clear(ui::Color::hex(0x232136));
        frame.paint(surface, 0, elapsed);
        return pixels;
    };
    const auto bundle = asset_bundle(file("apps/weather/assets/bundle.json"));
    const auto definition = bundle.resources.at(bundle.template_id)->bytes;
    {
        auto document = [&] {
            auto source = definition;
            return ui::CompositionDocument::from_tile(source);
        }();
        const auto moved = std::move(document);
        check(moved.fonts().size() == 2 && moved.fonts()[0].face.starts_with("ibm-plex-sans/"),
              "typed font face survives source lifetime and document move");
        check(moved.images().size() == 1 && moved.images()[0].field == "illustration" &&
                  moved.svg_fields().size() == 1,
              "SVG bindings are typed members, not a second JSON tree");
        auto varied = parse(definition, 32);
        auto &elements = varied.get<Object>()["texts"].get<Array>();
        elements[0].get<Object>()["overflow"] = Json("marquee");
        elements[1].get<Object>()["overflow"] = Json("ellipsis");
        std::swap(elements[0], elements[1]);
        const auto typed = ui::CompositionDocument::from_tile(varied.serialize());
        check(typed.composition().texts[0].overflow == ui::Overflow::Ellipsis &&
                  typed.composition().texts[1].overflow == ui::Overflow::Marquee,
              "overflow policy follows its element when elements change order");
#ifdef KAFEL_JSON_TEST_STATS
        const auto parses = kafel::json::parsed_documents;
        ui::CardTemplate once(definition);
        check(kafel::json::parsed_documents == parses + 1,
              "tile template decoded directly, without another JSON schema");
#endif

#ifdef KAFEL_JSON_TEST_STATS
        Receiver registry(
            [](const Snapshot &snapshot, const Resources &) -> Prepared {
                return std::make_shared<const PreparedTiles>(
                    std::vector<std::shared_ptr<const ui::CardFrame>>(snapshot.tiles.size()));
            },
            [](const Resource &resource) {
                ui::CardTemplate compiled(resource.bytes);
                const auto fields = compiled.svg_fields();
                return std::vector<std::string>(fields.begin(), fields.end());
            });
        const auto greeting = Json(Object{{"type", Json("hello")},
                                          {"service_id", Json("test")},
                                          {"name", Json("Test")},
                                          {"instance", Json("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")}});
        registry.hello("test", greeting);
        const auto metadata = Json(Object{{"type", Json("put")},
                                          {"id", Json(bundle.template_id)},
                                          {"media_type", Json("application/vnd.kafel.tile+json")},
                                          {"bytes", Json(double(definition.size()))}});
        const auto wire = metadata.serialize();
        const auto before_upload = kafel::json::parsed_documents;
        auto reply = registry.operation("test", metadata, wire, definition, 0);
        check(reply.has_value() && kafel::json::parsed_documents == before_upload + 1,
              "upload reuses compiled SVG bindings instead of parsing another template DOM");
#endif
        std::vector<unsigned char> plain_pixels(240 * 240 * 3, 150), inset_pixels(240 * 240 * 3, 150);
        ui::CardFrame plain(std::move(plain_pixels));
        ui::CardFrame inset(std::move(inset_pixels), 0, {}, 0, 4);
        const auto full = pixels_at(plain, 0), padded = pixels_at(inset, 0);
        check(full[0] == 150 && padded[0] != 150, "panel inset belongs to caller, not the renderer");
        check(!plain.same_visuals(inset), "inset participates in visual frame identity");
    }
    Json data(Object{{"temperature", Json(13.)},
                     {"apparent_temperature", Json(11.)},
                     {"wind_ms", Json(5.)},
                     {"precipitation_mm", Json(0.)},
                     {"description", Json("Zachmurzenie")},
                     {"illustration", Json(bundle.svg.at("partly-cloudy-day"))}});
    auto source = [&](std::string_view id) -> std::string_view {
        return bundle.resources.at(std::string(id))->bytes;
    };
    auto render = [&](const Json &values) {
        return ui::prepare_card(definition, values, source, font, 8388608, 4);
    };
    {
        ui::CardTemplate compiled(definition);
        ui::CardRenderCache cache;
        rejects([&] { cache.store_font("font", {}); }, "invalid cache entry");
        rejects([&] { cache.store_image("image", {}); }, "invalid cache entry");
        check(cache.storage_bytes() == 0, "invalid cache entries leave storage unchanged");
        const auto first = compiled.prepare(data, source, font, cache, 8388608, 4);
        const auto initial = cache.stats();
        const auto repeat = compiled.prepare(data, source, font, cache, 8388608, 4);
        check(first->same_visuals(*repeat) && cache.stats().font_preparations == initial.font_preparations &&
                  cache.stats().svg_rasterizations == initial.svg_rasterizations,
              "compiled template reuses unchanged fonts/SVG with identical pixels");
        std::set<std::string> ids;
        for (const auto &[name, id] : bundle.svg) {
            (void)name;
            ids.insert(id);
        }
        check(ids.size() > 32, "fixture exercises cache eviction");
        for (const auto &id : ids) {
            auto values = data;
            values.get<Object>()["illustration"] = Json(id);
            (void)compiled.prepare(values, source, font, cache, 8388608, 4);
            check(cache.storage_bytes() <= 8388608, "font/SVG cache remains within 8 MiB");
        }
        const auto after_eviction = compiled.prepare(data, source, font, cache, 8388608, 4);
        check(first->same_visuals(*after_eviction),
              "cache eviction never changes retained frames or output pixels");
        auto invalid = data;
        invalid.get<Object>()["temperature"] = Json(1000.);
        rejects([&] { (void)compiled.visual_key(invalid); }, "invalid_data");
        invalid = data;
        invalid.get<Object>()["extra"] = Json("unused");
        rejects([&] { (void)compiled.visual_key(invalid); }, "invalid_data");
        check(cache.stats().font_preparations == initial.font_preparations,
              "SVG turnover does not rebuild unchanged fonts");
    }
    const auto corner_frame = render(data);
    check(corner_frame->corner_radius() == 6, "prepared frame retains corner radius");
    const auto rounded_pixels = pixels_at(*corner_frame, 0);
    const auto corner = (4 * 240 + 4) * 3;
    check(rounded_pixels[corner] == 0x23 && rounded_pixels[corner + 1] == 0x21 &&
              rounded_pixels[corner + 2] == 0x36,
          "whole composition leaves host panel corner transparent");
    bool antialiased = false;
    for (int y = 4; y < 10; ++y)
        for (int x = 4; x < 10; ++x) {
            const auto i = (y * 240 + x) * 3;
            antialiased |= rounded_pixels[i] != 0x23 && rounded_pixels[i] != corner_frame->rgb()[i];
        }
    check(antialiased, "whole card corners retain antialiasing");
    const auto catalog = parse(file("apps/weather/assets/icons/catalog.json"));
    std::size_t cases = 0;
    for (const auto &entry : catalog.get("wmo").get<Array>()) {
        auto values = data;
        values.get<Object>()["description"] = entry.get("description");
        std::shared_ptr<const ui::CardFrame> day;
        for (const auto key : {"day", "night"}) {
            values.get<Object>()["illustration"] = Json(bundle.svg.at(entry.get(key).get<std::string>()));
            auto frame = render(values);
            check(frame->rgb().size() == 240 * 240 * 3 && frame->glyph_bytes() < 1048576,
                  "full WMO card stays bounded and prepares only used glyphs");
            if (key == std::string_view("day"))
                day = frame;
            else
                check((day->rgb() == frame->rgb()) == (entry.get("day") == entry.get("night")),
                      "day/night whole-card rendering follows supplied SVG only");
            ++cases;
        }
    }
    check(cases == 58, "every documented WMO/day-night combination rendered");
    for (auto temperature : {-100., -0.1, 0., 13.5, 100.}) {
        auto values = data;
        values.get<Object>()["temperature"] = Json(temperature);
        check(render(values)->rgb().size() == 172800, "temperature range fits value and complete unit");
    }
    auto minus_zero = data, zero = data;
    minus_zero.get<Object>()["temperature"] = Json(-0.1);
    zero.get<Object>()["temperature"] = Json(0.);
    check(render(minus_zero)->rgb() == render(zero)->rgb(), "no visible negative zero");
    auto half = data, rounded = data;
    half.get<Object>()["temperature"] = Json(13.5);
    rounded.get<Object>()["temperature"] = Json(14.);
    check(render(half)->rgb() == render(rounded)->rgb(), "integer half rounds away from zero");
    for (const char *field : {"temperature", "wind_ms", "description", "illustration"}) {
        auto invalid = data;
        invalid.get<Object>().erase(field);
        rejects([&] { render(invalid); }, "invalid_data");
    }
    for (auto value : {Json("13"), Json(), Json(true), Json(101.)}) {
        auto invalid = data;
        invalid.get<Object>()["temperature"] = value;
        rejects([&] { render(invalid); }, "invalid_data");
    }
    auto extra = data;
    extra.get<Object>()["unexpected"] = Json(1.);
    rejects([&] { render(extra); }, "invalid_data");
    auto invalid = data;
    invalid.get<Object>()["description"] = Json(std::string(65, 'x'));
    rejects([&] { render(invalid); }, "invalid_data");
    auto missing_glyph = parse(definition, 32);
    missing_glyph.get<Object>()["texts"].get<Array>()[0].get<Object>()["content"] = Json("Pogoda😀");
    rejects([&] { ui::prepare_card(missing_glyph.serialize(), data, source, font, 8388608, 4); },
            "invalid_data");
    for (auto member : {"schema", "fonts", "images"}) {
        auto bad = parse(definition, 32);
        bad.get<Object>()[member] = Json();
        rejects([&] { ui::validate_card_template(bad.serialize()); }, "invalid_template");
    }
    auto bad_image = parse(definition, 32);
    bad_image.get<Object>()["images"].get<Array>()[0].get<Object>()["fit"] = Json("stretch");
    rejects([&] { ui::validate_card_template(bad_image.serialize()); }, "invalid_template");
    auto bad_clip = parse(definition, 32);
    bad_clip.get<Object>()["images"].get<Array>()[0].get<Object>()["clip"] =
        Json(Array{Json(0.), Json(0.), Json(241.), Json(240.)});
    rejects([&] { ui::validate_card_template(bad_clip.serialize()); }, "invalid_template");
    for (auto svg : {"<svg width='10' height='10' unknown='x'/>",
                     "<svg width='10' height='10'><rect width='10' height='10' fill='url(#missing)'/></svg>",
                     "<svg width='10' height='10'><filter/></svg>", "junk<svg width='10' height='10'/>",
                     "<svg width='10' height='10'><text>bad</text></svg>"})
        rejects([&] { ui::SvgDocument rejected(svg); }, "invalid or unsupported static SVG");
    // Domain constraints apply to source values, not their rounded display representation.
    auto bounded = parse(definition, 32);
    for (auto &field : bounded.get<Object>()["fields"].get<Array>())
        if (field.get("name") == Json("wind_ms")) {
            field.get<Object>()["min"] = Json(4.94);
            field.get<Object>()["max"] = Json(4.96);
        }
    auto boundary = data;
    boundary.get<Object>()["wind_ms"] = Json(4.96);
    check(ui::prepare_card(bounded.serialize(), boundary, source, font, 8388608, 4)->rgb().size() == 172800,
          "valid source stays valid when display rounds past bound");
    rejects([&] { ui::prepare_card(definition, data, source, font, 1024, 4); }, "limit_exceeded");
    const auto waste = asset_bundle(file("apps/waste/assets/bundle.json"));
    {
        ui::SvgRasterizer raster;
        std::vector<unsigned char> rgba(212 * 64 * 4);
        for (unsigned mask = 0; mask < 64; ++mask) {
            ui::SvgDocument image(waste.resources.at(waste.svg.at(std::to_string(mask)))->bytes);
            raster.render(image, 212, 64, rgba);
            bool visible = false;
            for (std::size_t pixel = 3; pixel < rgba.size(); pixel += 4)
                visible |= rgba[pixel] != 0;
            check(visible == (mask != 0),
                  "every Heroicons fraction combination renders; zero is fully transparent");
        }
        ui::SvgDocument mixed(waste.resources.at(waste.svg.at("1"))->bytes);
        raster.render(mixed, 212, 64, rgba);
        const auto body = (38 * 212 + 27) * 4, lid = (12 * 212 + 27) * 4;
        check(rgba[body] == 0x5b && rgba[body + 1] == 0x5b && rgba[body + 2] == 0x5b && rgba[body + 3] == 255,
              "mixed bin body renders dark gray");
        check(rgba[lid] == 0xe6 && rgba[lid + 1] == 0xb8 && rgba[lid + 2] == 0 && rgba[lid + 3] == 255,
              "mixed bin lid renders yellow");
    }
    auto waste_template = parse(waste.resources.at(waste.template_id)->bytes);
    Json waste_data(Object{{"caption", Json("Dzisiaj")},
                           {"fractions", Json("Jutro: Zmieszane · Papier · Tworzywa · Kuchenne · Ogród")},
                           {"illustration", Json(waste.svg.at("61"))}});
    auto waste_source = [&](std::string_view id) -> std::string_view {
        return waste.resources.at(std::string(id))->bytes;
    };
    for (auto month : {"sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru"}) {
        auto date = waste_data;
        date.get<Object>()["caption"] = Json(std::string("28 ") + month);
        date.get<Object>()["fractions"] = Json("Papier");
        date.get<Object>()["illustration"] = Json(waste.svg.at("0"));
        auto frame = ui::prepare_card(waste_template.serialize(), date, waste_source, font, 8388608, 4);
        check(frame->rgb().size() == 172800 && frame->marquees().empty(),
              "whole date renders through the production waste template without a separate month field");
    }
    auto scrolling = ui::prepare_card(waste_template.serialize(), waste_data, waste_source, font, 8388608, 4);
    check(scrolling->marquees().size() == 1, "long fractions create one bounded prepared text layer");
    check(scrolling->marquees()[0].coverage.size() <= 262144, "persistent marquee coverage is bounded");
    for (auto &t : waste_template.get<Object>()["texts"].get<Array>())
        t.get<Object>()["overflow"] = Json("clip");
    auto clipped = ui::prepare_card(waste_template.serialize(), waste_data, waste_source, font, 8388608, 4);
    const auto clipped_pixels = pixels_at(*clipped, 0);
    check(pixels_at(*scrolling, 0) == clipped_pixels,
          "marquee initial raster equals original clipped TrueType text");
    check(pixels_at(*scrolling, 0) == pixels_at(*scrolling, 1200), "initial pause leaves pixels unchanged");
    const auto &layer = scrolling->marquees()[0];
    const auto travel = (static_cast<std::uint64_t>(layer.distance) * 1000 + 23) / 24;
    const auto at_end = 1200 + travel;
    check(pixels_at(*scrolling, 1700) != clipped_pixels, "fractions visibly move after pause");
    check(pixels_at(*scrolling, at_end) == pixels_at(*scrolling, at_end + 1200),
          "far end pauses without pixel changes");
    check(pixels_at(*scrolling, 2 * (1200 + travel)) == clipped_pixels,
          "return reaches exact original raster without jump");
    check(!scrolling->marquee_changed(at_end, at_end + 1200), "no dirty frame during end pause");
    check(scrolling->next_marquee_change(0).value() > 1200,
          "wake deadline is first changed pixel after pause");
    auto moved = pixels_at(*scrolling, 1700);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 240; ++x)
            if (x < layer.clip.x || x >= layer.clip.right() || y < layer.clip.y || y >= layer.clip.bottom())
                for (int c = 0; c < 3; ++c)
                    check(moved[(y * 240 + x) * 3 + c] == clipped_pixels[(y * 240 + x) * 3 + c],
                          "marquee never changes other fields or leaves its viewport");
    waste_data.get<Object>()["fractions"] = Json("Szkło");
    auto short_frame = ui::prepare_card(waste.resources.at(waste.template_id)->bytes, waste_data,
                                        waste_source, font, 8388608, 4);
    check(short_frame->marquees().empty() && !short_frame->next_marquee_change(0),
          "fitting fractions stay baked and have no wake deadline");
    check(pixels_at(*short_frame, 0) == pixels_at(*short_frame, 10000), "short fractions never move");
    waste_data.get<Object>()["fractions"] = Json(std::string(90, ' '));
    auto spaces = ui::prepare_card(waste.resources.at(waste.template_id)->bytes, waste_data, waste_source,
                                   font, 8388608, 4);
    check(spaces->marquees().empty(), "whitespace-only overflow cannot create an empty mask");
    waste_data.get<Object>()["fractions"] = Json("Jutro: Zmieszane · Papier · Tworzywa · Kuchenne · Ogród");
    auto bounded_marquees = parse(waste.resources.at(waste.template_id)->bytes);
    auto row = bounded_marquees.get("texts").get<Array>().back();
    auto &rows = bounded_marquees.get<Object>()["texts"].get<Array>();
    rows.clear();
    for (int i = 0; i < 64; ++i)
        rows.push_back(row);
    bounded_marquees.get<Object>()["images"] = Json(Array{});
    rejects(
        [&] { ui::prepare_card(bounded_marquees.serialize(), waste_data, waste_source, font, 8388608, 4); },
        "limit_exceeded");
    static_assert(!std::is_default_constructible_v<ui::CardFrame>);
    static_assert(!std::is_assignable_v<ui::CardFrame &, ui::CardFrame>);
    static_assert(std::is_same_v<decltype(scrolling->rgb()), const std::vector<unsigned char> &>);
    auto make_layer = [] {
        return ui::MarqueeLayer{{0, 0, 240, 240}, 4, 4, 2, 2, 1, {255, 255, 255}, {255, 255, 255, 255}};
    };
    auto make_frame = [&](ui::MarqueeLayer layer) {
        std::vector<ui::MarqueeLayer> layers;
        layers.push_back(std::move(layer));
        return ui::CardFrame(std::vector<unsigned char>(240 * 240 * 3, 0), 6, std::move(layers), 0, 4);
    };
    for (std::size_t size : {0u, 172799u, 172801u})
        rejects([&] { ui::CardFrame invalid(std::vector<unsigned char>(size), 6); }, "invalid_frame");
    for (int radius : {-1, 121, INT_MAX})
        rejects([&] { ui::CardFrame invalid(std::vector<unsigned char>(240 * 240 * 3), radius); },
                "invalid_frame");
    for (int field = 0; field < 12; ++field) {
        auto invalid = make_layer();
        switch (field) {
        case 0:
            invalid.clip.x = -1;
            break;
        case 1:
            invalid.clip.width = INT_MAX;
            break;
        case 2:
            invalid.width = 0;
            break;
        case 3:
            invalid.width = 4097;
            break;
        case 4:
            invalid.height = -1;
            break;
        case 5:
            invalid.height = INT_MAX;
            break;
        case 6:
            invalid.distance = 0;
            break;
        case 7:
            invalid.distance = INT_MAX;
            break;
        case 8:
            invalid.x = INT_MAX;
            break;
        case 9:
            invalid.y = INT_MIN;
            break;
        case 10:
            invalid.coverage.clear();
            break;
        case 11:
            invalid.clip.height = 0;
            break;
        }
        rejects([&] { (void)make_frame(invalid); }, "invalid_frame");
    }
    rejects(
        [&] {
            ui::CardFrame invalid(std::vector<unsigned char>(240 * 240 * 3), 6,
                                  std::vector<ui::MarqueeLayer>(65, make_layer()));
        },
        "invalid_frame");
    auto big = make_layer();
    big.width = 4096;
    big.height = 64;
    big.coverage.assign(4096 * 64, 255);
    rejects([&] { ui::CardFrame invalid(std::vector<unsigned char>(240 * 240 * 3), 6, {big, big}); },
            "invalid_frame");
    rejects(
        [&] {
            std::vector<unsigned char> oversized_rgb(240 * 240 * 3);
            oversized_rgb.reserve(240 * 240 * 3 + 1);
            ui::CardFrame invalid(std::move(oversized_rgb));
        },
        "invalid_frame");
    rejects(
        [&] {
            auto reserved = make_layer();
            reserved.coverage.reserve(262145);
            (void)make_frame(std::move(reserved));
        },
        "invalid_frame");
    rejects(
        [&] {
            auto first = make_layer(), second = make_layer();
            first.coverage.reserve(200000);
            second.coverage.reserve(200000);
            std::vector<ui::MarqueeLayer> layers;
            layers.push_back(std::move(first));
            layers.push_back(std::move(second));
            ui::CardFrame invalid(std::vector<unsigned char>(240 * 240 * 3), 6, std::move(layers), 0, 4);
        },
        "invalid_frame");
    rejects(
        [&] {
            std::vector<ui::MarqueeLayer> reserved;
            reserved.reserve(65);
            reserved.push_back(make_layer());
            ui::CardFrame invalid(std::vector<unsigned char>(240 * 240 * 3), 6, std::move(reserved));
        },
        "invalid_frame");
    auto bearing = make_layer();
    bearing.x = -2;
    bearing.y = -1;
    bearing.distance = 5000;
    const auto valid_bearing = make_frame(bearing);
    check(valid_bearing.storage_bytes() >= 240 * 240 * 3 + 36 + bearing.coverage.size(),
          "frame storage accounts for owned RGB, corners and coverage");
    const ui::CardFrame copied(valid_bearing);
    check(valid_bearing.same_visuals(copied), "copy preserves validated immutable visuals");
    std::vector<unsigned char> guard_pixels(240 * 240 * 3, 77);
    ui::Surface guard_surface(240, 240, guard_pixels);
    valid_bearing.paint(guard_surface, INT_MIN, UINT64_MAX);
    valid_bearing.paint(guard_surface, INT_MAX, UINT64_MAX);
    check(std::all_of(guard_pixels.begin(), guard_pixels.end(), [](auto p) { return p == 77; }),
          "extreme offscreen origins stay safe and write no pixels");
    ui::CardFrame maximum_radius(std::vector<unsigned char>(240 * 240 * 3), 120, {}, 0, 4);
    check(maximum_radius.corner_radius() == 116, "radius is clamped to actual inset panel extent");
    // This accepted template used to paint a marquee into the rounded panel's
    // transparent corner. Both overflowing and static text now share one mask.
    const std::string edge_template =
        R"({"schema":"kafel.tile/1","canvas":[240,240],"fonts":[{"id":"regular","face":"ibm-plex-sans/regular","repertoire":"H"}],"styles":{"body":{"font":"regular","size":22,"color":"#ffffff"}},"backdrop":{"base":"#393552","glow_center":[120,120],"glow_radius":[1,1],"glow_gain":[0,0,0],"stripe_start":240,"stripe_period":1,"stripe_width":0,"stripe_gain":"#000000","corner_radius":6},"fields":[],"images":[],"metrics":[],"texts":[{"at":[4,4],"clip":[0,0,240,240],"style":"body","content":"HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH","overflow":"marquee"}]})";
    const auto edge = ui::prepare_card(edge_template, Json(Object{}), source, font, 8388608, 4);
    auto edge_static = parse(edge_template);
    edge_static.get<Object>()["texts"].get<Array>()[0].get<Object>()["overflow"] = Json("clip");
    const auto edge_clipped =
        ui::prepare_card(edge_static.serialize(), Json(Object{}), source, font, 8388608, 4);
    check(pixels_at(*edge, 0) == pixels_at(*edge_clipped, 0),
          "accepted edge marquee initially matches static text under exactly the same panel mask");
    check(pixels_at(*edge, 1700) != pixels_at(*edge, 0), "edge text really scrolls");
    std::vector<unsigned char> panel_coverage(240 * 240 * 3);
    ui::Surface panel_mask(240, 240, panel_coverage);
    panel_mask.rounded_rectangle({4, 4, 232, 232}, 6, {255, 255, 255});
    for (auto phase : {0u, 1200u, 1700u, 5000u}) {
        const auto pixels = pixels_at(*edge, phase);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 240; ++x)
                if (panel_coverage[(y * 240 + x) * 3] == 0) {
                    const auto i = (y * 240 + x) * 3;
                    check(pixels[i] == 0x23 && pixels[i + 1] == 0x21 && pixels[i + 2] == 0x36,
                          "moving text cannot escape shared inset/corner coverage");
                }
    }
    std::puts("PASS immutable validated frame construction, safe bounds, exact static/marquee panel mask and "
              "scrolling");
    std::puts("PASS prepared bouncing fractions, exact static raster, pauses, return, clipping, fitting text "
              "and persistent mask budget");
    std::puts("PASS composition profile: 58 full WMO cards, Polish ellipsis, numeric formatting/bounds, "
              "glyph budget, SVG profile and invalid inputs");
}
