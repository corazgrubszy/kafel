#include "ui/card.hpp"
#include "ui/composition_json.hpp"
#include "ui/marquee.hpp"
#include "ui/svg.hpp"
#include "json/strict.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>
namespace ui {
namespace {
using namespace kafel::json;
bool valid_resource_id(std::string_view s) {
    return s.size() == 71 && s.starts_with("sha256:") &&
           s.substr(7).find_first_not_of("0123456789abcdef") == s.npos;
}
[[noreturn]] void fail() { throw std::invalid_argument("invalid_template"); }
std::string formatted(const Content &content, const Json &data) {
    if (content.field.empty())
        return std::string(content.literal);
    const auto &v = data.get(std::string(content.field));
    std::string value;
    if (content.format == ValueFormat::Text)
        value = text(v, 128, true);
    else {
        if (!v.is<double>())
            fail();
        char b[64];
        auto n = v.get<double>();
        if (content.format == ValueFormat::Integer)
            n = std::round(n);
        else
            n = std::round(n * 10) / 10;
        const auto formatted = std::to_chars(b, b + sizeof b, n == 0 ? 0 : n, std::chars_format::fixed,
                                             content.format == ValueFormat::Integer ? 0 : 1);
        if (formatted.ec != std::errc{})
            fail();
        value.assign(b, formatted.ptr);
        if (content.format == ValueFormat::General && value.ends_with(".0"))
            value.resize(value.size() - 2);
    }
    return std::string(content.prefix) + value + std::string(content.suffix);
}
} // namespace
CardFrame::CardFrame(std::vector<unsigned char> rgb, int radius, std::vector<MarqueeLayer> layers,
                     std::size_t glyph_bytes, int panel_inset)
    : rgb_(std::move(rgb)), marquees_(std::move(layers)), corner_radius_(radius), panel_inset_(panel_inset),
      glyph_bytes_(glyph_bytes) {
    constexpr int extent = 240;
    constexpr int coordinate_limit = 4096 + extent;
    constexpr int distance_limit = 1048576;
    if (rgb_.size() != extent * extent * 3 || rgb_.capacity() > extent * extent * 3 || radius < 0 ||
        radius > extent / 2 || panel_inset < 0 || panel_inset >= extent / 2 || marquees_.capacity() > 64)
        throw std::invalid_argument("invalid_frame");
    std::size_t coverage_bytes = 0, coverage_reserved = 0;
    for (const auto &layer : marquees_) {
        const auto &clip = layer.clip;
        if (clip.x < 0 || clip.y < 0 || clip.width < 1 || clip.height < 1 ||
            static_cast<std::int64_t>(clip.x) + clip.width > extent ||
            static_cast<std::int64_t>(clip.y) + clip.height > extent || layer.width < 1 ||
            layer.width > 4096 || layer.height < 1 || layer.height > 4096 || layer.x < -coordinate_limit ||
            layer.x > coordinate_limit || layer.y < -coordinate_limit || layer.y > coordinate_limit ||
            layer.distance < 1 || layer.distance > distance_limit)
            throw std::invalid_argument("invalid_frame");
        const auto bytes = static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height);
        if (bytes > 262144 - coverage_bytes || layer.coverage.size() != bytes ||
            layer.coverage.capacity() > 262144 - coverage_reserved)
            throw std::invalid_argument("invalid_frame");
        coverage_bytes += bytes;
        coverage_reserved += layer.coverage.capacity();
    }
    // The wire profile permits radius120. As for Surface::rounded_rectangle,
    // clamp that radius to half the actual host panel rather than the canvas.
    const int panel_extent = extent - 2 * panel_inset_;
    corner_radius_ = std::min(radius, panel_extent / 2);
    if (corner_radius_) {
        std::vector<unsigned char> mask(static_cast<std::size_t>(corner_radius_) * corner_radius_ * 3);
        Surface coverage(corner_radius_, corner_radius_, mask);
        coverage.rounded_rectangle({0, 0, panel_extent, panel_extent}, corner_radius_, {255, 255, 255});
        corner_alpha_.resize(static_cast<std::size_t>(corner_radius_) * corner_radius_);
        if (corner_alpha_.capacity() > extent * extent / 4)
            throw std::invalid_argument("invalid_frame");
        for (std::size_t i = 0; i < corner_alpha_.size(); ++i)
            corner_alpha_[i] = mask[i * 3];
    }
}
std::size_t CardFrame::storage_bytes() const noexcept {
    std::size_t result =
        rgb_.capacity() + corner_alpha_.capacity() + marquees_.capacity() * sizeof(MarqueeLayer);
    for (const auto &layer : marquees_)
        result += layer.coverage.capacity();
    return result;
}
bool CardFrame::same_visuals(const CardFrame &other) const noexcept {
    return panel_inset_ == other.panel_inset_ && corner_radius_ == other.corner_radius_ &&
           rgb_ == other.rgb_ && marquees_ == other.marquees_;
}
void CardFrame::paint(Surface &surface, int origin_x, std::uint64_t elapsed) const noexcept {
    constexpr int extent = 240;
    const int inset = panel_inset_, edge = extent - inset;
    const auto outer = surface.clip();
    // Intersect in 64 bits so even an off-screen INT_MIN/INT_MAX origin is safe.
    const auto left = std::max<std::int64_t>(outer.x, static_cast<std::int64_t>(origin_x) + inset);
    const auto right = std::min<std::int64_t>(outer.right(), static_cast<std::int64_t>(origin_x) + edge);
    const int top = std::max(outer.y, inset), bottom = std::min(outer.bottom(), edge);
    if (left >= right || top >= bottom)
        return;
    const int first_x = static_cast<int>(left - origin_x), last_x = static_cast<int>(right - origin_x);
    std::array<int, 64> shifts{};
    for (std::size_t i = 0; i < marquees_.size(); ++i) {
        const auto &layer = marquees_[i];
        shifts[i] = MarqueeText::offset(layer.distance + layer.clip.width, layer.clip.width, elapsed);
    }
    // One small row is enough: combine text with cached RGB, then apply the
    // panel mask exactly once to the complete composition. No frame allocation.
    std::array<unsigned char, extent * 3> row;
    for (int y = top; y < bottom; ++y) {
        std::copy_n(rgb_.data() + y * extent * 3, row.size(), row.data());
        for (std::size_t i = 0; i < marquees_.size(); ++i) {
            const auto &layer = marquees_[i];
            if (y < layer.clip.y || y >= layer.clip.bottom() || y < layer.y || y >= layer.y + layer.height)
                continue;
            const int start = layer.x - shifts[i];
            const int x0 = std::max({first_x, layer.clip.x, start});
            const int x1 = std::min({last_x, layer.clip.right(), start + layer.width});
            const unsigned channels[] = {layer.color.r, layer.color.g, layer.color.b};
            for (int x = x0; x < x1; ++x) {
                const auto index = static_cast<std::size_t>(y - layer.y) * layer.width + x - start;
                const unsigned alpha = layer.coverage[index];
                for (unsigned k = 0; k < 3; ++k)
                    row[x * 3 + k] = static_cast<unsigned char>(
                        (row[x * 3 + k] * (255 - alpha) + channels[k] * alpha + 127) / 255);
            }
        }
        for (int x = first_x; x < last_x; ++x) {
            const int cx = std::min(x - inset, edge - 1 - x), cy = std::min(y - inset, edge - 1 - y);
            const unsigned alpha =
                cx < corner_radius_ && cy < corner_radius_ ? corner_alpha_[cy * corner_radius_ + cx] : 255;
            surface.blend(static_cast<int>(static_cast<std::int64_t>(origin_x) + x), y,
                          {row[x * 3], row[x * 3 + 1], row[x * 3 + 2]}, alpha);
        }
    }
}
bool CardFrame::marquee_changed(std::uint64_t before, std::uint64_t after) const noexcept {
    for (const auto &layer : marquees_)
        if (MarqueeText::offset(layer.distance + layer.clip.width, layer.clip.width, before) !=
            MarqueeText::offset(layer.distance + layer.clip.width, layer.clip.width, after))
            return true;
    return false;
}
std::optional<std::uint64_t> CardFrame::next_marquee_change(std::uint64_t elapsed) const noexcept {
    std::optional<std::uint64_t> result;
    for (const auto &layer : marquees_)
        if (auto next =
                MarqueeText::next_change(layer.distance + layer.clip.width, layer.clip.width, elapsed);
            next && (!result || *next < *result))
            result = next;
    return result;
}
namespace {
struct PreparedTexts {
    std::vector<std::string> values;
    std::vector<MarqueeLayer> marquees;
};
PreparedTexts prepare_texts(const Composition &composition, const std::vector<std::string> &formatted_texts,
                            std::span<const TextFont *const> faces) {
    std::vector<std::string> text_values(composition.texts.size());
    std::vector<MarqueeLayer> marquees;
    std::size_t coverage_bytes = 0;
    for (std::size_t i = 0; i < composition.texts.size(); ++i) {
        const auto &element = composition.texts[i];
        auto value = formatted_texts[i];
        (void)faces[element.font]->measure(value, element.style);
        if (element.overflow == Overflow::Ellipsis &&
            faces[element.font]->measure(value, element.style) > element.clip.width) {
            while (!value.empty() &&
                   faces[element.font]->measure(value + "…", element.style) > element.clip.width) {
                auto last = value.size() - 1;
                while (last && ((static_cast<unsigned char>(value[last]) & 192) == 128))
                    --last;
                value.resize(last);
            }
            value += "…";
            if (faces[element.font]->measure(value, element.style) > element.clip.width)
                fail();
        }
        const int distance = std::max(
            0, element.x + static_cast<int>(std::ceil(faces[element.font]->measure(value, element.style))) -
                   element.clip.right());
        const auto ink = faces[element.font]->ink_bounds(value, element.style);
        if (element.overflow == Overflow::Marquee && distance > 0 && ink.width > 0 && ink.height > 0) {
            const auto bytes = static_cast<std::size_t>(ink.width) * ink.height;
            if (ink.width > 4096 || coverage_bytes + bytes > 262144)
                throw std::invalid_argument("limit_exceeded");
            coverage_bytes += bytes;
            std::vector<unsigned char> pixels(bytes * 3);
            Surface mask(ink.width, ink.height, pixels);
            auto white = element.style;
            white.color = {255, 255, 255};
            faces[element.font]->draw(mask, {0, 0, ink.width, ink.height}, -ink.x, -ink.y, value, white);
            MarqueeLayer layer{.clip = element.clip,
                               .x = element.x + ink.x,
                               .y = element.y + ink.y,
                               .width = ink.width,
                               .height = ink.height,
                               .distance = distance,
                               .color = element.style.color,
                               .coverage = std::vector<unsigned char>(bytes)};
            for (std::size_t p = 0; p < bytes; ++p)
                layer.coverage[p] = pixels[p * 3];
            marquees.push_back(std::move(layer));
            value.clear();
        }
        text_values[i] = std::move(value);
    }
    return {std::move(text_values), std::move(marquees)};
}
struct PreparedFonts {
    std::vector<std::shared_ptr<const TextFont>> owned;
    std::vector<const TextFont *> faces;
    std::size_t glyph_bytes = 0;
};
} // namespace
struct CardRenderCache::Impl {
    struct Entry {
        std::string key;
        std::shared_ptr<const TextFont> font;
        std::shared_ptr<const std::vector<unsigned char>> image;
        std::size_t bytes;
        std::uint64_t used;
    };
    std::vector<Entry> entries;
    std::size_t bytes = 0;
    std::uint64_t clock = 0;
    PreparationStats stats;
    void trim_fonts(std::size_t maximum) {
        auto masks = [&] {
            std::size_t n = 0;
            for (const auto &e : entries)
                if (e.font)
                    n += e.font->prepared_bytes();
            return n;
        };
        while (masks() > maximum) {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->font && (oldest == entries.end() || it->used < oldest->used))
                    oldest = it;
            bytes -= oldest->bytes;
            entries.erase(oldest);
        }
    }
    Entry *find(const std::string &key) {
        for (auto &e : entries)
            if (e.key == key) {
                e.used = ++clock;
                return &e;
            }
        return nullptr;
    }
    void insert(Entry e) {
        constexpr std::size_t limit = 8388608;
        e.bytes += e.key.capacity() + sizeof(Entry);
        if (e.bytes > limit)
            return;
        while (!entries.empty() && (entries.size() >= 32 || bytes + e.bytes > limit)) {
            auto it = std::min_element(entries.begin(), entries.end(),
                                       [](const auto &a, const auto &b) { return a.used < b.used; });
            bytes -= it->bytes;
            entries.erase(it);
        }
        bytes += e.bytes;
        e.used = ++clock;
        entries.push_back(std::move(e));
    }
};
CardRenderCache::CardRenderCache() : impl_(std::make_unique<Impl>()) {}
CardRenderCache::~CardRenderCache() = default;
PreparationStats CardRenderCache::stats() const noexcept { return impl_->stats; }
std::size_t CardRenderCache::storage_bytes() const noexcept { return impl_->bytes; }
std::shared_ptr<const TextFont> CardRenderCache::find_font(const std::string &key) {
    auto entry = impl_->find(key);
    return entry ? entry->font : nullptr;
}
std::shared_ptr<const std::vector<unsigned char>> CardRenderCache::find_image(const std::string &key) {
    auto entry = impl_->find(key);
    return entry ? entry->image : nullptr;
}
void CardRenderCache::store_font(std::string key, std::shared_ptr<const TextFont> font) {
    if (!font)
        throw std::invalid_argument("invalid cache entry");
    ++impl_->stats.font_preparations;
    impl_->insert({std::move(key), font, {}, font->storage_bytes(), 0});
}
void CardRenderCache::store_image(std::string key, std::shared_ptr<const std::vector<unsigned char>> image) {
    if (!image)
        throw std::invalid_argument("invalid cache entry");
    ++impl_->stats.svg_rasterizations;
    impl_->insert({std::move(key), {}, image, image->capacity(), 0});
}
void CardRenderCache::trim_fonts(std::size_t mask_bytes) { impl_->trim_fonts(mask_bytes); }
namespace {
PreparedFonts prepare_fonts(const CompositionDocument &document, const std::vector<std::string> &text_values,
                            const std::vector<std::string> &metric_values, const FontSource &font_source,
                            std::size_t mask_budget, CardRenderCache &cache) {
    std::vector<std::shared_ptr<const TextFont>> fonts;
    std::vector<const TextFont *> faces;
    std::size_t glyph_bytes = 0, font_index = 0;
    const auto &composition = document.composition();
    for (const auto &font_definition : document.fonts()) {
        // Each size prepares only its actual strings, not repertoire × every size.
        struct Run {
            std::string text;
            TextStyle style;
        };
        std::vector<Run> strings;
        auto add = [&](std::string value, TextStyle style) {
            auto remaining = std::string_view(value);
            while (!remaining.empty()) {
                const auto before = remaining;
                const auto character = kafel::utf8::decode(remaining);
                if (!character.valid ||
                    font_definition.repertoire.find(before.substr(0, before.size() - remaining.size())) ==
                        font_definition.repertoire.npos)
                    throw std::invalid_argument("invalid_data");
            }
            strings.push_back({std::move(value), style});
        };
        for (std::size_t i = 0; i < composition.texts.size(); ++i)
            if (composition.texts[i].font == font_index) {
                auto value = text_values[i];
                if (composition.texts[i].overflow == Overflow::Ellipsis)
                    value += "…";
                add(std::move(value), composition.texts[i].style);
            }
        for (std::size_t i = 0; i < composition.metrics.size(); ++i) {
            const auto &metric = composition.metrics[i];
            if (metric.font == font_index) {
                const auto &value = metric_values[i];
                for (auto style : metric.sizes)
                    add(value, style);
                add(std::string(metric.unit), metric.unit_style);
            }
        }
        std::vector<TextFont::GlyphRun> runs;
        runs.reserve(strings.size());
        for (const auto &run : strings)
            runs.push_back({run.text, run.style});
        const auto face_name = font_definition.face;
        // Font masks depend on codepoints and sizes, not colour or tracking.
        // Stable run keys also avoid rebuilding the unchanged caption font.
        std::string key = "font:" + std::string(face_name) + ":";
        for (const auto &run : runs) {
            key += std::to_string(run.text.size()) + ":";
            key += run.text;
            char size[32];
            auto end = std::to_chars(size, size + sizeof(size), run.style.em_px);
            if (end.ec != std::errc{})
                throw std::invalid_argument("invalid_template");
            key.append(size, end.ptr);
            key += ';';
        }
        auto font = cache.find_font(key);
        if (!font) {
            // Cached masks plus this candidate's masks stay within the
            // profile's 8 MiB. Candidate glyphs are already limited below;
            // count them conservatively even when also present in the LRU.
            cache.trim_fonts(8388608 - mask_budget);
            font = std::make_shared<TextFont>(font_source(face_name), runs, mask_budget - glyph_bytes);
            cache.store_font(std::move(key), font);
        }
        if (font->prepared_bytes() > mask_budget - glyph_bytes)
            throw std::invalid_argument("limit_exceeded");
        fonts.push_back(std::move(font));
        glyph_bytes += fonts.back()->prepared_bytes();
        faces.push_back(fonts.back().get());
        ++font_index;
    }
    return {std::move(fonts), std::move(faces), glyph_bytes};
}
std::vector<unsigned char> prepare_images(const CompositionDocument &document, const Json &data,
                                          const ResourceSource &resource, CardRenderCache &cache) {
    std::vector<unsigned char> layer(240 * 240 * 4);
    for (const auto &image : document.images()) {
        const auto [x, y, w, h] = image.bounds;
        const auto clip = image.clip;
        const auto id = data.get(std::string(image.field)).get<std::string>();
        auto key = "svg:" + id + ":" + std::to_string(w) + "x" + std::to_string(h);
        auto pixels = cache.find_image(key);
        if (!pixels) {
            SvgDocument svg(resource(id));
            SvgRasterizer raster;
            auto image = std::make_shared<std::vector<unsigned char>>(static_cast<std::size_t>(w) * h * 4);
            raster.render(svg, w, h, *image);
            cache.store_image(std::move(key), image);
            pixels = std::move(image);
        }
        const auto &rgba = *pixels;
        for (int dy = std::max(y, clip.y); dy < std::min(y + h, clip.bottom()); ++dy)
            for (int dx = std::max(x, clip.x); dx < std::min(x + w, clip.right()); ++dx) {
                auto src = (static_cast<std::size_t>(dy - y) * w + dx - x) * 4;
                auto dst = (dy * 240 + dx) * 4;
                unsigned sa = rgba[src + 3], da = layer[dst + 3], alpha = sa + (da * (255 - sa) + 127) / 255;
                for (unsigned k = 0; k < 3; ++k)
                    layer[dst + k] =
                        alpha
                            ? static_cast<unsigned char>(
                                  (rgba[src + k] * sa + layer[dst + k] * da * (255 - sa) / 255 + alpha / 2) /
                                  alpha)
                            : 0;
                layer[dst + 3] = static_cast<unsigned char>(alpha);
            }
    }
    return layer;
}
} // namespace
struct CardTemplate::Impl {
    CompositionDocument document;
    explicit Impl(std::string_view source) : document(CompositionDocument::from_tile(source)) {}
    Json values(const Json &data) const try {
        const auto &c = document.composition();
        if (!data.is<Object>() || data.get<Object>().size() != c.fields.size() + document.svg_fields().size())
            fail();
        Json values = data;
        for (const auto &field : c.fields) {
            if (!data.contains(std::string(field.name)))
                fail();
            const auto &v = data.get(std::string(field.name));
            if (field.format == ValueFormat::Text)
                text(v, field.maximum_bytes, true);
            else {
                if (!v.is<double>() || !std::isfinite(v.get<double>()) || v.get<double>() < field.minimum ||
                    v.get<double>() > field.maximum)
                    throw std::invalid_argument("invalid_data");
                auto n = v.get<double>();
                values.get<Object>()[std::string(field.name)] =
                    Json(field.format == ValueFormat::Integer ? std::round(n) : std::round(n * 10) / 10);
            }
        }
        for (const auto &name : document.svg_fields())
            if (!data.contains(std::string(name)) ||
                !valid_resource_id(text(data.get(std::string(name)), 71)))
                fail();
        return values;
    } catch (const std::invalid_argument &) {
        throw std::invalid_argument("invalid_data");
    }
};
CardTemplate::CardTemplate(std::string_view source, std::span<const std::string_view> allowed_faces) try
    : impl_(std::make_unique<Impl>(source)) {
    if (!allowed_faces.empty())
        for (const auto &font : impl_->document.fonts()) {
            if (std::find(allowed_faces.begin(), allowed_faces.end(), font.face) == allowed_faces.end())
                fail();
        }
} catch (const std::invalid_argument &) {
    throw std::invalid_argument("invalid_template");
}
CardTemplate::~CardTemplate() = default;
std::span<const std::string_view> CardTemplate::svg_fields() const noexcept {
    return impl_->document.svg_fields();
}
std::string CardTemplate::visual_key(const Json &data) const {
    const auto values = impl_->values(data);
    const auto &c = impl_->document.composition();
    std::string key;
    auto append = [&](std::string_view value) {
        key += std::to_string(value.size()) + ":";
        key += value;
    };
    for (const auto &t : c.texts)
        append(formatted(t.content, values));
    for (const auto &m : c.metrics)
        append(formatted(m.content, values));
    for (const auto &image : impl_->document.images())
        append(data.get(std::string(image.field)).get<std::string>());
    return key;
}
void validate_card_template(std::string_view definition, std::span<const std::string_view> allowed_faces) {
    CardTemplate compiled(definition, allowed_faces);
}
std::shared_ptr<const CardFrame> CardTemplate::prepare(const Json &data, const ResourceSource &resource,
                                                       const FontSource &font_source, CardRenderCache &cache,
                                                       std::size_t transient_limit, int panel_inset) const {
    try {
        const auto &document = impl_->document;
        const auto &definition = document.composition();
        const auto values = impl_->values(data);
        constexpr std::size_t scratch_bytes = 2 * 1024 * 1024;
        if (transient_limit <= scratch_bytes)
            throw std::invalid_argument("limit_exceeded");
        ++cache.impl_->stats.frame_renders;
        std::vector<std::string> text_values, metric_values;
        for (const auto &text : definition.texts)
            text_values.push_back(formatted(text.content, values));
        for (const auto &metric : definition.metrics)
            metric_values.push_back(formatted(metric.content, values));
        auto fonts = prepare_fonts(document, text_values, metric_values, font_source,
                                   transient_limit - scratch_bytes, cache);
        auto texts = prepare_texts(definition, text_values, fonts.faces);
        auto images = prepare_images(document, values, resource, cache);
        std::vector<FieldRule> rules(definition.fields.begin(), definition.fields.end());
        std::vector<Field> bindings;
        for (auto &rule : rules) {
            const auto &v = values.get(std::string(rule.name));
            if (rule.format != ValueFormat::Text) {
                const double scale = rule.format == ValueFormat::Integer ? 1 : 10;
                rule.minimum = std::floor(rule.minimum * scale) / scale;
                rule.maximum = std::ceil(rule.maximum * scale) / scale;
                bindings.push_back({rule.name, v.get<double>()});
            } else
                bindings.push_back({rule.name, std::string_view(v.get<std::string>())});
        }
        Composition composition = definition;
        std::vector<TextElement> elements(definition.texts.begin(), definition.texts.end());
        for (std::size_t i = 0; i < elements.size(); ++i)
            elements[i].content = Content{.literal = texts.values[i]};
        composition.texts = elements;
        composition.fields = rules;
        const int corner_radius = definition.backdrop.corner_radius;
        composition.backdrop.corner_radius = 0;
        std::vector<unsigned char> rgb(240 * 240 * 3);
        Surface surface(240, 240, rgb);
        CompositionPainter painter(composition, fonts.faces);
        RgbaImage overlay{240, 240, images};
        painter.paint(surface, bindings, &overlay);
        return std::make_shared<const CardFrame>(std::move(rgb), corner_radius, std::move(texts.marquees),
                                                 fonts.glyph_bytes, panel_inset);
    } catch (const std::bad_alloc &) {
        throw std::invalid_argument("limit_exceeded");
    } catch (const std::invalid_argument &error) {
        const std::string_view code = error.what();
        if (code == "limit_exceeded" || code == "invalid_svg")
            throw;
        throw std::invalid_argument("invalid_data");
    }
}
std::shared_ptr<const CardFrame> prepare_card(std::string_view definition, const Json &data,
                                              const ResourceSource &resource, const FontSource &fonts,
                                              std::size_t transient_limit, int panel_inset) {
    CardTemplate compiled(definition);
    CardRenderCache cache;
    return compiled.prepare(data, resource, fonts, cache, transient_limit, panel_inset);
}
} // namespace ui
