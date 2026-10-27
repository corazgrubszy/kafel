#pragma once
#include "geometry.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
namespace ui {
// Base geometry is independent of gestures and painting. Spans are 1..3.
class RibbonLayout {
  public:
    static constexpr std::size_t capacity = 16;
    bool configure(PixelRect viewport, std::span<const unsigned> spans, int gap = 4) noexcept;
    PixelRect viewport() const noexcept { return viewport_; }
    PixelRect tile(std::size_t index) const noexcept { return tiles_[index]; }
    std::size_t size() const noexcept { return size_; }
    int maximum_offset() const noexcept;
    int anchor(std::size_t index) const noexcept;

  private:
    PixelRect viewport_{};
    std::array<PixelRect, capacity> tiles_{};
    std::size_t size_ = 0;
};
// Horizontal navigation only. Each tile keeps its identity and base rectangle;
// this controller supplies the shared rigid offset, never a flattened bitmap.
class RibbonController {
  public:
    enum class Paging { Free, Adjacent };
    explicit RibbonController(std::uint64_t idle_timeout_ms = 60000, Paging paging = Paging::Free) noexcept
        : idle_timeout_(idle_timeout_ms), paging_(paging) {}
    enum class Event { Down, Move, Up, Cancel };
    bool configure(const RibbonLayout &layout, std::uint64_t now_ms) noexcept;
    void pointer(Event event, int x, int y, std::uint64_t now_ms) noexcept;
    bool select(std::size_t tile, std::uint64_t now_ms, bool animate = true) noexcept;
    void update(std::uint64_t now_ms) noexcept;
    double offset(std::uint64_t now_ms) const noexcept;
    bool animating(std::uint64_t now_ms) const noexcept;
    bool dragging() const noexcept { return held_; }
    std::optional<std::size_t> grabbed_tile() const noexcept { return grabbed_; }
    std::size_t selected() const noexcept { return selected_; }
    std::optional<std::uint64_t> idle_deadline() const noexcept;
    const RibbonLayout &layout() const noexcept { return layout_; }

  private:
    RibbonLayout layout_;
    double from_ = 0, target_ = 0, drag_offset_ = 0, down_offset_ = 0, velocity_ = 0, release_velocity_ = 0;
    int down_x_ = 0, down_y_ = 0, last_x_ = 0;
    bool held_ = false, horizontal_ = false, rejected_ = false;
    std::size_t selected_ = 0, gesture_origin_ = 0;
    std::optional<std::size_t> grabbed_;
    std::uint64_t started_ = 0, last_move_ = 0, last_interaction_ = 0;
    static constexpr std::uint64_t duration = 600;
    std::uint64_t idle_timeout_ = 60000;
    Paging paging_ = Paging::Free;
    void settle(std::uint64_t now_ms, bool fling) noexcept;
};
} // namespace ui
