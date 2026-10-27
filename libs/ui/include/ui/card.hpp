#pragma once
#include "picojson.h"
#include "surface.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace ui {
class TextFont;
// Prepared monochrome coverage; rendering never retains a font or allocates.
struct MarqueeLayer {
    PixelRect clip;
    int x = 0, y = 0, width = 0, height = 0, distance = 0;
    Color color;
    std::vector<unsigned char> coverage;
    bool operator==(const MarqueeLayer &) const = default;
};
// Immutable, validated 240x240 DASH tile. The caller selects the panel inset;
// static RGB and moving text are composited before corner coverage.
// Creation also limits allocated vector capacities, not just visible lengths.
class CardFrame {
  public:
    explicit CardFrame(std::vector<unsigned char> rgb, int radius = 0, std::vector<MarqueeLayer> layers = {},
                       std::size_t glyph_bytes = 0, int panel_inset = 0);
    CardFrame(const CardFrame &) = default;
    CardFrame &operator=(const CardFrame &) = delete;
    const std::vector<unsigned char> &rgb() const noexcept { return rgb_; }
    const std::vector<MarqueeLayer> &marquees() const noexcept { return marquees_; }
    int corner_radius() const noexcept { return corner_radius_; }
    std::size_t glyph_bytes() const noexcept { return glyph_bytes_; }
    std::size_t storage_bytes() const noexcept;
    bool same_visuals(const CardFrame &) const noexcept;
    void paint(Surface &, int origin_x, std::uint64_t elapsed) const noexcept;
    bool marquee_changed(std::uint64_t before, std::uint64_t after) const noexcept;
    std::optional<std::uint64_t> next_marquee_change(std::uint64_t elapsed) const noexcept;

  private:
    std::vector<unsigned char> rgb_, corner_alpha_;
    std::vector<MarqueeLayer> marquees_;
    int corner_radius_, panel_inset_;
    std::size_t glyph_bytes_; // preparation cost; masks belong to the worker cache, not this frame
};

using ResourceSource = std::function<std::string_view(std::string_view)>;
using FontSource = std::function<std::span<const unsigned char>(std::string_view)>;
struct PreparationStats {
    std::size_t font_preparations = 0, svg_rasterizations = 0, frame_renders = 0;
};
// Worker-owned LRU. Retains at most 8 MiB of fonts/rasterized SVG and 32 entries.
// Frames never borrow cache storage; eviction cannot affect an existing page.
class CardRenderCache {
  public:
    CardRenderCache();
    ~CardRenderCache();
    CardRenderCache(const CardRenderCache &) = delete;
    CardRenderCache &operator=(const CardRenderCache &) = delete;
    PreparationStats stats() const noexcept;
    std::size_t storage_bytes() const noexcept;
    std::shared_ptr<const TextFont> find_font(const std::string &key);
    std::shared_ptr<const std::vector<unsigned char>> find_image(const std::string &key);
    void store_font(std::string key, std::shared_ptr<const TextFont>);
    void store_image(std::string key, std::shared_ptr<const std::vector<unsigned char>>);
    void trim_fonts(std::size_t mask_bytes);

  private:
    friend class CardTemplate;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Validated geometry/styles/bindings are compiled once, independently of data.
class CardTemplate {
  public:
    explicit CardTemplate(std::string_view definition, std::span<const std::string_view> allowed_faces = {});
    ~CardTemplate();
    CardTemplate(const CardTemplate &) = delete;
    CardTemplate &operator=(const CardTemplate &) = delete;
    // Validates all data, but identifies only formatted text and image references.
    std::string visual_key(const picojson::value &data) const;
    std::span<const std::string_view> svg_fields() const noexcept;
    std::shared_ptr<const CardFrame> prepare(const picojson::value &, const ResourceSource &,
                                             const FontSource &, CardRenderCache &,
                                             std::size_t transient_limit = 8388608,
                                             int panel_inset = 0) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Generic immutable composition preparation. No service names, Linux or WMO.
std::shared_ptr<const CardFrame> prepare_card(std::string_view definition, const picojson::value &data,
                                              const ResourceSource &, const FontSource &,
                                              std::size_t transient_limit = 8388608, int panel_inset = 0);
void validate_card_template(std::string_view definition,
                            std::span<const std::string_view> allowed_faces = {});
} // namespace ui
