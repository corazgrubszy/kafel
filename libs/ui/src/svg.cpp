#include "ui/svg.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef KAFEL_SVG_MEMORY_STATS
#include "svg_memory_stats.hpp"
#include <cstdlib>
#include <limits>
namespace {
ui::detail::SvgMemoryStats svg_stats{};
struct alignas(std::max_align_t) SvgAllocation {
    std::size_t size;
};
void *svg_malloc(std::size_t size) {
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(SvgAllocation))
        return nullptr;
    auto *block = static_cast<SvgAllocation *>(std::malloc(sizeof(SvgAllocation) + size));
    if (!block)
        return nullptr;
    block->size = size;
    svg_stats.live += size;
    svg_stats.peak = std::max(svg_stats.peak, svg_stats.live);
    ++svg_stats.allocations;
    return block + 1;
}
void svg_free(void *pointer) {
    if (!pointer)
        return;
    auto *block = static_cast<SvgAllocation *>(pointer) - 1;
    svg_stats.live -= block->size;
    std::free(block);
}
void *svg_realloc(void *pointer, std::size_t size) {
    if (!pointer)
        return svg_malloc(size);
    if (!size) {
        svg_free(pointer);
        return nullptr;
    }
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(SvgAllocation))
        return nullptr;
    auto *old = static_cast<SvgAllocation *>(pointer) - 1;
    const auto previous = old->size;
    auto *block = static_cast<SvgAllocation *>(std::realloc(old, sizeof(SvgAllocation) + size));
    if (!block)
        return nullptr;
    block->size = size;
    svg_stats.live = svg_stats.live - previous + size;
    svg_stats.peak = std::max(svg_stats.peak, svg_stats.live);
    ++svg_stats.allocations;
    return block + 1;
}
} // namespace
#define malloc svg_malloc
#define realloc svg_realloc
#define free svg_free
#endif
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#ifdef KAFEL_SVG_MEMORY_STATS
#undef malloc
#undef realloc
#undef free
namespace ui::detail {
SvgMemoryStats svg_memory_stats() noexcept { return svg_stats; }
} // namespace ui::detail
#endif
namespace ui {
namespace {
[[noreturn]] void invalid() { throw std::invalid_argument("invalid or unsupported static SVG"); }
struct Profile {
    std::array<std::string_view, 32> stack{};
    std::size_t depth = 0, roots = 0;
    bool valid = true;
    std::set<std::string> ids, gradients, paint_refs;
    static void start(void *context, const char *tag, const char **attributes) {
        auto &p = *static_cast<Profile *>(context);
        const std::string_view name = tag;
        constexpr std::array allowed = {
            "svg",  "g",        "defs",    "path",           "rect",           "circle", "ellipse",
            "line", "polyline", "polygon", "linearGradient", "radialGradient", "stop"};
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            p.valid = false;
        if (p.depth == 0) {
            if (name != "svg" || ++p.roots != 1)
                p.valid = false;
        }
        if (p.depth == p.stack.size()) {
            p.valid = false;
            return;
        }
        p.stack[p.depth++] = name;
        for (std::size_t i = 0; attributes[i]; i += 2) {
            const std::string_view key = attributes[i], value = attributes[i + 1];
            constexpr std::array attrs = {"id",
                                          "xmlns",
                                          "xmlns:xlink",
                                          "width",
                                          "height",
                                          "viewBox",
                                          "preserveAspectRatio",
                                          "x",
                                          "y",
                                          "x1",
                                          "y1",
                                          "x2",
                                          "y2",
                                          "cx",
                                          "cy",
                                          "fx",
                                          "fy",
                                          "r",
                                          "rx",
                                          "ry",
                                          "d",
                                          "points",
                                          "transform",
                                          "fill",
                                          "stroke",
                                          "fill-rule",
                                          "clip-rule",
                                          "opacity",
                                          "fill-opacity",
                                          "stroke-opacity",
                                          "stroke-width",
                                          "stroke-linecap",
                                          "stroke-linejoin",
                                          "stroke-miterlimit",
                                          "stroke-dasharray",
                                          "stroke-dashoffset",
                                          "gradientUnits",
                                          "gradientTransform",
                                          "spreadMethod",
                                          "offset",
                                          "stop-color",
                                          "stop-opacity"};
            if (std::find(attrs.begin(), attrs.end(), key) == attrs.end())
                p.valid = false;
            if (key == "id") {
                if (value.empty() || value.size() > 128 || !p.ids.insert(std::string(value)).second)
                    p.valid = false;
                if (name == "linearGradient" || name == "radialGradient")
                    p.gradients.insert(std::string(value));
            }
            if (value.find("url(") != value.npos) {
                if ((key != "fill" && key != "stroke") || !value.starts_with("url(#") ||
                    !value.ends_with(")"))
                    p.valid = false;
                else
                    p.paint_refs.insert(std::string(value.substr(5, value.size() - 6)));
            }

            if (key == "href" || key == "xlink:href" || key == "filter" || key == "clip-path" ||
                key == "mask" || key == "style" || key.starts_with("on"))
                p.valid = false;
            // All paint references are local gradient IDs; there is no resource loader.
            if (value.find("url(") != value.npos && !value.starts_with("url(#"))
                p.valid = false;
        }
    }
    static void content(void *context, const char *text) {
        auto &p = *static_cast<Profile *>(context);
        if (std::string_view(text).find_first_not_of(" \t\r\n") != std::string_view::npos)
            p.valid = false;
    }
    static void end(void *context, const char *tag) {
        auto &p = *static_cast<Profile *>(context);
        if (!p.depth || p.stack[p.depth - 1] != tag) {
            p.valid = false;
            return;
        }
        --p.depth;
    }
};
void validate_source(std::string_view source) {
    if (source.empty() || source.size() > SvgDocument::maximum_bytes || source.find('\0') != source.npos ||
        source.find("<!DOCTYPE") != source.npos || source.find("<!ENTITY") != source.npos)
        invalid();
    std::vector<char> copy(source.begin(), source.end());
    copy.push_back(0);
    Profile profile;
    nsvg__parseXML(copy.data(), Profile::start, Profile::end, Profile::content, &profile);
    for (const auto &id : profile.paint_refs)
        if (!profile.gradients.contains(id))
            profile.valid = false;
    if (!profile.valid || profile.depth || profile.roots != 1)
        invalid();
}
void validate_geometry(NSVGimage *image) {
    if (!image || !std::isfinite(image->width) || !std::isfinite(image->height) || image->width < 1 ||
        image->height < 1 || image->width > 4096 || image->height > 4096)
        invalid();
    std::size_t shapes = 0, points = 0;
    for (auto *shape = image->shapes; shape; shape = shape->next) {
        if (++shapes > 1024 || !std::isfinite(shape->opacity) || !std::isfinite(shape->strokeWidth) ||
            shape->strokeWidth < 0 || shape->strokeWidth > 1e6f || !std::isfinite(shape->miterLimit) ||
            shape->miterLimit < 0 || !std::isfinite(shape->strokeDashOffset) || shape->strokeDashCount < 0 ||
            shape->strokeDashCount > 8)
            invalid();
        for (int i = 0; i < shape->strokeDashCount; ++i)
            if (!std::isfinite(shape->strokeDashArray[i]) || shape->strokeDashArray[i] < 0)
                invalid();
        for (auto *path = shape->paths; path; path = path->next) {
            if (path->npts < 1 || path->npts > 65536 || (points += path->npts) > 65536)
                invalid();
            for (int i = 0; i < path->npts * 2; ++i)
                if (!std::isfinite(path->pts[i]) || std::abs(path->pts[i]) > 1e6f)
                    invalid();
        }
    }
}
// NanoSVG converts screen coordinates to signed 10-bit fixed-point values.
// Raw SVG bounds alone are insufficient: a small intrinsic viewport can greatly
// magnify coordinates and stroke joins. Keep a generous margin below INT_MAX,
// and bound stroke subdivision before entering the vendor rasterizer.
void validate_raster_geometry(const NSVGimage *image, double scale, double x, double y) {
    constexpr double coordinate_limit = 262144, stroke_limit = 4096;
    for (const auto *shape = image->shapes; shape; shape = shape->next) {
        if (std::abs(shape->strokeDashOffset * scale) > coordinate_limit)
            invalid();
        for (int i = 0; i < shape->strokeDashCount; ++i)
            if (shape->strokeDashArray[i] * scale > coordinate_limit)
                invalid();
        const double stroke = shape->stroke.type == NSVG_PAINT_NONE ? 0 : shape->strokeWidth * scale;
        const double expansion = stroke * std::max(1.0, static_cast<double>(shape->miterLimit));
        if (stroke > stroke_limit || expansion > coordinate_limit)
            invalid();
        for (const auto *path = shape->paths; path; path = path->next)
            for (int i = 0; i < path->npts; ++i) {
                const double px = path->pts[i * 2] * scale + x, py = path->pts[i * 2 + 1] * scale + y;
                if (std::abs(px) + expansion > coordinate_limit ||
                    std::abs(py) + expansion > coordinate_limit)
                    invalid();
            }
    }
}
} // namespace
struct SvgDocument::Impl {
    NSVGimage *image = nullptr;
    explicit Impl(std::string_view source) {
        validate_source(source);
        std::vector<char> copy(source.begin(), source.end());
        copy.push_back(0);
        image = nsvgParse(copy.data(), "px", 96);
        try {
            validate_geometry(image);
        } catch (...) {
            nsvgDelete(image);
            throw;
        }
    }
    ~Impl() { nsvgDelete(image); }
};
struct SvgRasterizer::Impl {
    NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
    Impl() {
        if (!rasterizer)
            throw std::bad_alloc();
    }
    ~Impl() { nsvgDeleteRasterizer(rasterizer); }
};
SvgDocument::SvgDocument(std::string_view source) : impl_(std::make_unique<Impl>(source)) {}
SvgDocument::~SvgDocument() = default;
SvgDocument::SvgDocument(SvgDocument &&) noexcept = default;
float SvgDocument::width() const noexcept { return impl_->image->width; }
float SvgDocument::height() const noexcept { return impl_->image->height; }
SvgRasterizer::SvgRasterizer() : impl_(std::make_unique<Impl>()) {}
SvgRasterizer::~SvgRasterizer() = default;
void SvgRasterizer::render(const SvgDocument &document, int width, int height,
                           std::span<unsigned char> rgba) {
    if (width < 1 || height < 1 || width > 512 || height > 512 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4)
        invalid();
    const auto scale = std::min(width / document.width(), height / document.height());
    const auto x = (width - document.width() * scale) / 2, y = (height - document.height() * scale) / 2;
    validate_raster_geometry(document.impl_->image, scale, x, y);
    nsvgRasterize(impl_->rasterizer, document.impl_->image, x, y, scale, rgba.data(), width, height,
                  width * 4);
}
std::string read_svg_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 0 || size > static_cast<std::streamoff>(SvgDocument::maximum_bytes))
        invalid();
    std::string source(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(source.data(), size) || file.peek() != std::char_traits<char>::eof())
        invalid();
    return source;
}
} // namespace ui
