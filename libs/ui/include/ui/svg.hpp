#pragma once
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
namespace ui {
// Bounded static SVG subset for controlled artwork, not a browser SVG engine.
class SvgDocument {
  public:
    static constexpr std::size_t maximum_bytes = 65536;
    explicit SvgDocument(std::string_view svg);
    ~SvgDocument();
    SvgDocument(SvgDocument &&) noexcept;
    SvgDocument(const SvgDocument &) = delete;
    SvgDocument &operator=(const SvgDocument &) = delete;
    float width() const noexcept;
    float height() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class SvgRasterizer;
};
class SvgRasterizer {
  public:
    SvgRasterizer();
    ~SvgRasterizer();
    SvgRasterizer(const SvgRasterizer &) = delete;
    SvgRasterizer &operator=(const SvgRasterizer &) = delete;
    // Straight-alpha RGBA8, aspect ratio preserved and centered; at most 512x512.
    // Caller owns the exact-sized output. Internal scratch storage is reused.
    void render(const SvgDocument &, int width, int height, std::span<unsigned char> rgba);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string read_svg_file(const char *path);
} // namespace ui
