#pragma once
#include "composition.hpp"
#include "picojson.h"
#include <memory>
namespace ui {
struct CompositionFont {
    std::string_view id, repertoire;
    std::span<const TextStyle> styles;
    std::string_view face{};
};
struct ImageElement {
    PixelRect bounds, clip;
    std::string_view field;
};
// Owns decoded strings, geometry and styles. Views/painters are valid only
// while this document lives. Loading allocates; painting does not.
class CompositionDocument {
  public:
    static constexpr std::size_t maximum_bytes = 65536;
    explicit CompositionDocument(std::string_view json);
    static CompositionDocument from_tile(std::string_view json);
    static CompositionDocument from_file(const char *path);
    ~CompositionDocument();
    CompositionDocument(const CompositionDocument &) = delete;
    CompositionDocument &operator=(const CompositionDocument &) = delete;
    CompositionDocument(CompositionDocument &&) noexcept;
    CompositionDocument &operator=(CompositionDocument &&) = delete;
    const Composition &composition() const noexcept;
    std::span<const CompositionFont> fonts() const noexcept;
    std::span<const ImageElement> images() const noexcept;
    std::span<const std::string_view> svg_fields() const noexcept;

  private:
    CompositionDocument(picojson::value decoded, bool tile);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ui
