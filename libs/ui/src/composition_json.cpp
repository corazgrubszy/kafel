#include "ui/composition_json.hpp"
#include "json/strict.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace ui {
namespace {
using Json = picojson::value;
[[noreturn]] void fail(const char *reason) { throw std::invalid_argument(reason); }
void object(const Json &value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional = {}) {
    kafel::json::shape(value, required, optional);
}
const Json &member(const Json &value, const char *key) { return value.get(key); }
std::string_view string(const Json &value, std::size_t maximum = 128) {
    return kafel::json::text_view(value, maximum, true);
}
double number(const Json &value, double minimum, double maximum) {
    if (!value.is<double>())
        fail("expected template number");
    const auto result = value.get<double>();
    if (!std::isfinite(result) || result < minimum || result > maximum)
        fail("template number out of range");
    return result;
}
int integer(const Json &value, int minimum, int maximum) {
    const auto result = number(value, minimum, maximum);
    if (std::floor(result) != result)
        fail("expected template integer");
    return static_cast<int>(result);
}
const picojson::array &array(const Json &value, std::size_t minimum, std::size_t maximum) {
    if (!value.is<picojson::array>())
        fail("expected template array");
    const auto &result = value.get<picojson::array>();
    if (result.size() < minimum || result.size() > maximum)
        fail("template array limit");
    return result;
}
Color color(const Json &value) {
    auto text = string(value, 7);
    if (text.size() != 7 || text[0] != '#')
        fail("expected #RRGGBB");
    unsigned rgb = 0;
    const auto result = std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + 7)
        fail("invalid template color");
    return Color::hex(rgb);
}
Point point(const Json &value) {
    const auto &xy = array(value, 2, 2);
    return {static_cast<float>(number(xy[0], 0, 2048)), static_cast<float>(number(xy[1], 0, 2048))};
}
PixelRect rectangle(const Json &value) {
    const auto &r = array(value, 4, 4);
    return {integer(r[0], 0, 2048), integer(r[1], 0, 2048), integer(r[2], 1, 2048), integer(r[3], 1, 2048)};
}
ValueFormat format(const Json &value) {
    const auto name = string(value, 16);
    if (name == "text")
        return ValueFormat::Text;
    if (name == "integer")
        return ValueFormat::Integer;
    if (name == "general")
        return ValueFormat::General;
    fail("unsupported value format");
}
Content content(const Json &value) {
    if (value.is<std::string>())
        return {.literal = string(value)};
    object(value, {"field", "format"}, {"prefix", "suffix"});
    Content result{.field = string(member(value, "field"), 32), .format = format(member(value, "format"))};
    if (result.field.empty())
        fail("empty binding name");
    if (value.contains("prefix"))
        result.prefix = string(member(value, "prefix"));
    if (value.contains("suffix"))
        result.suffix = string(member(value, "suffix"));
    return result;
}
} // namespace
struct CompositionDocument::Impl {
    Json root;
    Composition definition{};
    std::vector<CompositionFont> fonts;
    std::vector<std::vector<TextStyle>> font_styles;
    struct Style {
        unsigned font;
        TextStyle text;
    };
    std::map<std::string_view, Style> styles;
    std::vector<TextElement> texts;
    std::vector<std::vector<TextStyle>> metric_sizes;
    std::vector<MetricElement> metrics;
    std::vector<FieldRule> fields;
    std::vector<ImageElement> images;
    std::vector<std::string_view> svg_fields;
    bool tile_profile;
    const Style &style(const Json &value) const {
        const auto found = styles.find(string(value, 32));
        if (found == styles.end())
            fail("unknown style reference");
        return found->second;
    }
    static Json decode(std::string_view input) { return kafel::json::parse(input, 32); }
    explicit Impl(Json decoded, bool tile = false) : root(std::move(decoded)), tile_profile(tile) {
        if (tile) {
            object(root, {"schema", "canvas", "fonts", "styles", "backdrop", "texts", "metrics", "fields",
                          "images"});
            if (member(root, "schema") != Json("kafel.tile/1"))
                fail("unsupported tile schema");
        } else {
            object(root, {"version", "canvas", "fonts", "styles", "backdrop", "texts", "metrics", "fields"});
            integer(member(root, "version"), 1, 1);
        }
        const auto &canvas = array(member(root, "canvas"), 2, 2);
        definition.width = integer(canvas[0], 1, 2048);
        definition.height = integer(canvas[1], 1, 2048);
        if (tile && (definition.width != 240 || definition.height != 240))
            fail("tile canvas must be 240x240");
        read_fonts();
        read_styles();
        read_backdrop();
        read_texts();
        read_metrics();
        read_fields();
        read_images();
        definition.texts = texts;
        definition.metrics = metrics;
        definition.fields = fields;
        validate_composition(definition, fonts.size());
    }
    void read_fonts() {
        const bool tile = tile_profile;
        const auto &font_items = array(member(root, "fonts"), 1, tile ? 4 : 8);
        fonts.reserve(font_items.size());
        font_styles.resize(font_items.size());
        for (const auto &font : font_items) {
            if (tile)
                object(font, {"id", "face", "repertoire"});
            else
                object(font, {"id", "repertoire"});
            auto id = string(member(font, "id"), 32);
            if (tile)
                kafel::json::identifier(member(font, "id"));
            if (id.empty() || std::any_of(fonts.begin(), fonts.end(), [&](auto f) { return f.id == id; }))
                fail("duplicate or empty font id");
            auto repertoire = string(member(font, "repertoire"), 2048);
            if (repertoire.empty())
                fail("empty font repertoire");
            if (tile) {
                std::set<char32_t> glyphs;
                auto remaining = repertoire;
                while (!remaining.empty())
                    glyphs.insert(kafel::utf8::decode(remaining).codepoint);
                if (glyphs.size() > 512)
                    fail("glyph limit");
            }
            if (tile && string(member(font, "face"), 32).empty())
                fail("empty font face");
            fonts.push_back({.id = id,
                             .repertoire = repertoire,
                             .styles = {},
                             .face = tile ? string(member(font, "face"), 32) : std::string_view{}});
        }
    }
    void read_styles() {
        const bool tile = tile_profile;
        const auto &style_items = member(root, "styles");
        if (!style_items.is<picojson::object>() || style_items.get<picojson::object>().empty() ||
            style_items.get<picojson::object>().size() > (tile ? 16u : 64u))
            fail("style limit");
        std::set<std::pair<std::string_view, double>> sizes;
        for (const auto &[name, item] : style_items.get<picojson::object>()) {
            if (tile)
                kafel::json::identifier(Json(name));
            if (name.empty() || name.size() > 32)
                fail("invalid style id");
            object(item, {"font", "size", "color"}, {"tracking"});
            const auto id = string(member(item, "font"), 32);
            if (tile)
                kafel::json::identifier(member(item, "font"));
            const auto found = std::find_if(fonts.begin(), fonts.end(), [&](auto f) { return f.id == id; });
            if (found == fonts.end())
                fail("unknown font reference");
            const auto index = static_cast<unsigned>(found - fonts.begin());
            TextStyle text{static_cast<float>(number(member(item, "size"), 1, 128)),
                           color(member(item, "color")), 0};
            if (item.contains("tracking"))
                text.tracking =
                    static_cast<float>(number(member(item, "tracking"), tile ? -8 : -128, tile ? 8 : 128));
            styles.emplace(name, Style{index, text});
            font_styles[index].push_back(text);
            sizes.emplace(fonts[index].face, member(item, "size").get<double>());
        }
        if (tile && sizes.size() > 8)
            fail("font size limit");
        for (std::size_t i = 0; i < fonts.size(); ++i) {
            if (font_styles[i].empty())
                fail("unused font resource");
            fonts[i].styles = font_styles[i];
        }
    }
    void read_backdrop() {
        const auto &b = member(root, "backdrop");
        object(b, {"base", "glow_center", "glow_radius", "glow_gain", "stripe_start", "stripe_period",
                   "stripe_width", "stripe_gain", "corner_radius"});
        const auto &gain = array(member(b, "glow_gain"), 3, 3);
        definition.backdrop = {color(member(b, "base")),
                               point(member(b, "glow_center")),
                               point(member(b, "glow_radius")),
                               {static_cast<float>(number(gain[0], 0, 255)),
                                static_cast<float>(number(gain[1], 0, 255)),
                                static_cast<float>(number(gain[2], 0, 255))},
                               integer(member(b, "stripe_start"), 0, 2048),
                               integer(member(b, "stripe_period"), 1, 4096),
                               integer(member(b, "stripe_width"), 0, 4096),
                               color(member(b, "stripe_gain")),
                               integer(member(b, "corner_radius"), 0, 1024)};
    }
    void read_texts() {
        const bool tile = tile_profile;
        for (const auto &item : array(member(root, "texts"), 0, 64)) {
            if (tile)
                object(item, {"at", "clip", "style", "content", "overflow"});
            else
                object(item, {"at", "clip", "style", "content"});
            Overflow overflow = Overflow::Clip;
            if (tile) {
                const auto mode = string(member(item, "overflow"), 8);
                if (mode == "ellipsis")
                    overflow = Overflow::Ellipsis;
                else if (mode == "marquee")
                    overflow = Overflow::Marquee;
                else if (mode != "clip")
                    fail("invalid overflow mode");
            }
            const auto &at = array(member(item, "at"), 2, 2);
            const auto &selected = style(member(item, "style"));
            texts.push_back({.x = integer(at[0], 0, 2048),
                             .y = integer(at[1], 0, 2048),
                             .clip = rectangle(member(item, "clip")),
                             .font = selected.font,
                             .style = selected.text,
                             .content = content(member(item, "content")),
                             .overflow = overflow});
        }
    }
    void read_metrics() {
        const auto &metric_items = array(member(root, "metrics"), 0, 64);
        metric_sizes.reserve(metric_items.size());
        metrics.reserve(metric_items.size());
        for (const auto &item : metric_items) {
            object(item, {"at", "clip", "sizes", "fit_width", "content", "unit", "unit_style", "unit_y",
                          "unit_gap"});
            const auto &at = array(member(item, "at"), 2, 2);
            const auto &unit = style(member(item, "unit_style"));
            metric_sizes.emplace_back();
            for (const auto &size : array(member(item, "sizes"), 1, 8)) {
                const auto &selected = style(size);
                if (selected.font != unit.font)
                    fail("metric styles must use one font");
                metric_sizes.back().push_back(selected.text);
            }
            metrics.push_back({.x = integer(at[0], 0, 2048),
                               .y = integer(at[1], 0, 2048),
                               .clip = rectangle(member(item, "clip")),
                               .font = unit.font,
                               .sizes = metric_sizes.back(),
                               .fit_width = static_cast<float>(number(member(item, "fit_width"), 1, 2048)),
                               .content = content(member(item, "content")),
                               .unit = string(member(item, "unit"), 32),
                               .unit_style = unit.text,
                               .unit_y = integer(member(item, "unit_y"), 0, 2048),
                               .unit_gap = integer(member(item, "unit_gap"), 0, 2048)});
        }
    }
    void read_fields() {
        const bool tile = tile_profile;
        std::set<std::string_view> names;
        for (const auto &item : array(member(root, "fields"), 0, 32)) {
            const auto name = string(member(item, "name"), 32);
            if (tile)
                kafel::json::identifier(member(item, "name"));
            if (!names.insert(name).second)
                fail("duplicate field name");
            if (tile && member(item, "format") == Json("svg")) {
                object(item, {"name", "format"});
                svg_fields.push_back(name);
                continue;
            }
            object(item, {"name", "format"}, {"min", "max", "max_bytes"});
            FieldRule rule{string(member(item, "name"), 32), format(member(item, "format"))};
            if (rule.format == ValueFormat::Text) {
                if (item.contains("min") || item.contains("max") || !item.contains("max_bytes"))
                    fail("invalid text field rule");
                rule.maximum_bytes = integer(member(item, "max_bytes"), 1, 128);
            } else {
                if (item.contains("max_bytes") || !item.contains("min") || !item.contains("max"))
                    fail("invalid numeric field rule");
                rule.minimum = number(member(item, "min"), -1e100, 1e100);
                rule.maximum = number(member(item, "max"), -1e100, 1e100);
            }
            fields.push_back(rule);
        }
    }
    void read_images() {
        const bool tile = tile_profile;
        if (tile) {
            for (const auto &item : array(member(root, "images"), 0, 4)) {
                object(item, {"at", "size", "clip", "source", "fit"});
                object(member(item, "source"), {"field"});
                const auto field = string(member(item, "source").get("field"), 32);
                kafel::json::identifier(member(item, "source").get("field"));
                if (member(item, "fit") != Json("contain") ||
                    std::find(svg_fields.begin(), svg_fields.end(), field) == svg_fields.end())
                    fail("invalid image source or fit");
                const auto &at = array(member(item, "at"), 2, 2);
                const auto &size = array(member(item, "size"), 2, 2);
                const auto clip = rectangle(member(item, "clip"));
                if (clip.right() > 240 || clip.bottom() > 240)
                    fail("image clip outside tile");
                images.push_back({.bounds = {integer(at[0], 0, 239), integer(at[1], 0, 239),
                                             integer(size[0], 1, 240), integer(size[1], 1, 240)},
                                  .clip = clip,
                                  .field = field});
            }
            if (texts.size() + metrics.size() + images.size() > 64)
                fail("element limit");
        }
    }
};
CompositionDocument::CompositionDocument(std::string_view json)
    : impl_(std::make_unique<Impl>(Impl::decode(json))) {}
CompositionDocument::CompositionDocument(picojson::value decoded, bool tile)
    : impl_(std::make_unique<Impl>(std::move(decoded), tile)) {}
CompositionDocument CompositionDocument::from_tile(std::string_view input) {
    if (input.size() > 32768)
        fail("tile template too large");
    return CompositionDocument(Impl::decode(input), true);
}
std::span<const ImageElement> CompositionDocument::images() const noexcept { return impl_->images; }
std::span<const std::string_view> CompositionDocument::svg_fields() const noexcept {
    return impl_->svg_fields;
}
CompositionDocument::~CompositionDocument() = default;
CompositionDocument::CompositionDocument(CompositionDocument &&) noexcept = default;
const Composition &CompositionDocument::composition() const noexcept { return impl_->definition; }
std::span<const CompositionFont> CompositionDocument::fonts() const noexcept { return impl_->fonts; }
CompositionDocument CompositionDocument::from_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (!file || length < 0 || length > static_cast<std::streamoff>(maximum_bytes))
        fail("cannot read bounded template file");
    std::string input(static_cast<std::size_t>(length), '\0');
    file.seekg(0);
    if (!file.read(input.data(), length))
        fail("cannot read template file");
    if (file.peek() != std::char_traits<char>::eof())
        fail("template file grew during read");
    return CompositionDocument(input);
}
} // namespace ui
