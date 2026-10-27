#include "memory.hpp"
#include "ui/composition_json.hpp"
#include "json/document.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL JSON composition: %s\n", message);
        std::exit(1);
    }
}
template <class F> void rejects(F operation, std::string_view reason = {}) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument &error) {
        rejected = true;
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              "expected validation reason");
    }
    check(rejected, "malformed template must be rejected");
}
} // namespace
void test_json_composition(const char *regular, const char *bold, const char *path) {
    using namespace ui;
    std::ifstream file(path);
    std::string input((std::istreambuf_iterator<char>(file)), {});
    check(!input.empty(), "read template fixture");
    const auto fixture = *kafel::json::document(input);
    auto bad = [&](auto mutate) {
        auto json = fixture;
        mutate(json);
        rejects([&] { CompositionDocument rejected(json.serialize()); });
    };
    using Object = picojson::object;
    using Array = picojson::array;
    using Json = picojson::value;
    auto texts = [](Json &json) -> Array & { return json.get<Object>().at("texts").get<Array>(); };
    auto styles = [](Json &json) -> Object & { return json.get<Object>().at("styles").get<Object>(); };
    auto backdrop = [](Json &json) -> Object & { return json.get<Object>().at("backdrop").get<Object>(); };
    bad([](Json &j) { j.get<Object>()["unknown"] = Json(1.0); });
    bad([](Json &j) { j.get<Object>().erase("canvas"); });
    bad([](Json &j) { j.get<Object>()["version"] = Json(2.0); });
    bad([](Json &j) { j.get<Object>()["version"] = Json(true); });
    bad([](Json &j) { j.get<Object>().at("canvas").get<Array>()[0] = Json(240.5); });
    bad([](Json &j) { j.get<Object>().at("canvas").get<Array>()[0] = Json(2049.0); });
    bad([&](Json &j) { styles(j).at("title").get<Object>()["color"] = Json(std::string("#12ZZ34")); });
    bad([&](Json &j) { styles(j).at("title").get<Object>()["size"] = Json(129.0); });
    bad([&](Json &j) { styles(j).at("title").get<Object>()["font"] = Json(std::string("arbitrary.ttf")); });
    bad([&](Json &j) { texts(j)[0].get<Object>()["style"] = Json(std::string("missing")); });
    bad([&](Json &j) { texts(j)[0].get<Object>()["content"] = Json(std::string("bad\ntext")); });
    bad([&](Json &j) { texts(j)[0].get<Object>()["content"] = Json(std::string("\xc0\xaf", 2)); });
    bad([&](Json &j) { texts(j)[0].get<Object>()["content"] = Json(std::string(129, 'x')); });
    bad([&](Json &j) { texts(j)[0].get<Object>()["at"] = Json(Array{Json(241.0), Json(0.0)}); });
    bad([&](Json &j) { texts(j)[0].get<Object>()["clip"] = Json(Array{Json(0.0)}); });
    bad([&](Json &j) {
        auto item = texts(j)[0];
        texts(j).assign(65, item);
    });
    bad([&](Json &j) {
        auto item = texts(j)[0];
        texts(j).assign(64, item);
    }); // aggregate element bound
    bad([&](Json &j) { backdrop(j)["glow_radius"] = Json(Array{Json(0.0), Json(32.0)}); });
    bad([&](Json &j) { backdrop(j)["stripe_width"] = Json(100.0); });
    bad([&](Json &j) { backdrop(j)["corner_radius"] = Json(121.0); });
    bad([](Json &j) {
        auto &fonts = j.get<Object>().at("fonts").get<Array>();
        fonts.push_back(fonts[0]);
    });
    bad([](Json &j) {
        j.get<Object>().at("fonts").get<Array>()[0].get<Object>()["path"] = Json(std::string("/etc/file"));
    });
    bad([](Json &j) {
        j.get<Object>().at("metrics").get<Array>()[0].get<Object>()["sizes"] = Json(Array{});
    });
    bad([](Json &j) {
        auto &rule = j.get<Object>().at("fields").get<Array>()[0].get<Object>();
        rule["max"] = Json(-101.0);
    });
    bad([](Json &j) {
        auto &rules = j.get<Object>().at("fields").get<Array>();
        rules.push_back(rules[0]);
    });
    bad([&](Json &j) {
        texts(j)[2].get<Object>().at("content").get<Object>()["field"] = Json(std::string("missing"));
    });
    bad([&](Json &j) {
        texts(j)[2].get<Object>().at("content").get<Object>()["format"] = Json(std::string("printf"));
    });
    rejects([&] { CompositionDocument rejected(input + "{}"); });
    rejects([] { CompositionDocument rejected(std::string(CompositionDocument::maximum_bytes + 1, ' ')); });
    rejects([] { CompositionDocument rejected(std::string(33, '[') + "0" + std::string(33, ']')); });
    rejects([] { CompositionDocument rejected("{\"version\":1e999}"); });
    rejects([] { CompositionDocument rejected("{\"version\":1,\"version\":1}"); }, "invalid_message");
    rejects([] { CompositionDocument rejected("{\"version\":1,\"ver\\u0073ion\":1}"); }, "invalid_message");
    rejects([] { CompositionDocument rejected("{\"styles\":{\"x\":1,\"x\":2}}"); }, "invalid_message");
    rejects([] {
        CompositionDocument rejected = CompositionDocument::from_file("/nonexistent/kafel-template.json");
    });
    // Own all strings and arrays even after the caller destroys its JSON bytes.
    CompositionDocument loaded(input);
    input.assign(input.size(), 'x');
    input.clear();
    input.shrink_to_fit();
    CompositionDocument owned(std::move(loaded));
    std::vector<std::unique_ptr<TextFont>> fonts;
    std::vector<const TextFont *> faces;
    for (const auto &font : owned.fonts()) {
        fonts.push_back(
            std::make_unique<TextFont>(font.id == "regular" ? regular : bold, font.repertoire, font.styles));
        faces.push_back(fonts.back().get());
    }
    CompositionPainter painter(owned.composition(), faces);
    const std::array values = {Field{"temperature", 13.0}, Field{"apparent_temperature", 11.0},
                               Field{"wind_ms", 5.0}, Field{"precipitation_mm", 0.0},
                               Field{"description", std::string_view("Sucho")}};
    std::array<unsigned char, 240 * 240 * 3> pixels{};
    Surface surface(240, 240, pixels);
    const auto allocations = ui_test::memory().allocations;
    painter.paint(surface, values);
    check(ui_test::memory().allocations == allocations, "JSON rendering does not allocate");
    const auto stable = pixels;
    painter.paint(surface, values);
    check(pixels == stable, "JSON render is stable");
    auto edited = fixture;
    texts(edited)[0].get<Object>()["content"] = Json(std::string("Deszcz"));
    CompositionDocument edited_document(edited.serialize());
    CompositionPainter edited_painter(edited_document.composition(), faces);
    const auto original = pixels;
    edited_painter.paint(surface, values);
    check(pixels != original, "changing JSON changes the rendered composition without C++ changes");
    painter.paint(surface, values);
    check(pixels == original, "original document stays independent of edited template");
    // A completely different canvas and data field use the same file format.
    const auto small =
        R"({"version":1,"canvas":[48,32],"fonts":[{"id":"regular","repertoire":"0123456789 V"}],"styles":{"value":{"font":"regular","size":14,"color":"#ffffff"}},"backdrop":{"base":"#232136","glow_center":[10,10],"glow_radius":[10,10],"glow_gain":[0,0,0],"stripe_start":48,"stripe_period":1,"stripe_width":0,"stripe_gain":"#000000","corner_radius":2},"texts":[{"at":[2,2],"clip":[0,0,48,32],"style":"value","content":{"field":"voltage","format":"general","suffix":" V"}}],"metrics":[],"fields":[{"name":"voltage","format":"general","min":0,"max":100}]})";
    CompositionDocument other(small);
    TextFont voltage_font(regular, other.fonts()[0].repertoire, other.fonts()[0].styles);
    const std::array<const TextFont *, 1> voltage_fonts = {&voltage_font};
    CompositionPainter voltage(other.composition(), voltage_fonts);
    std::array<unsigned char, 48 * 32 * 3> voltage_pixels{};
    Surface voltage_surface(48, 32, voltage_pixels);
    const std::array voltage_data = {Field{"voltage", 24.0}};
    voltage.paint(voltage_surface, voltage_data);
    check(std::any_of(voltage_pixels.begin(), voltage_pixels.end(), [](auto byte) { return byte > 200; }),
          "non-weather JSON paints bound data");
    std::puts("PASS JSON templates: schema, duplicates, references, limits, UTF-8, ownership, non-weather "
              "data and stable allocation-free render");
}
