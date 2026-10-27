#include "ui/ribbon.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ui {
namespace {
constexpr double edge_travel = 36;
}
bool RibbonLayout::configure(PixelRect viewport, std::span<const unsigned> spans, int gap) noexcept {
    if (viewport.width < 3 || viewport.height < 1 || viewport.width > 4096 || gap < 0 || gap > 64 ||
        viewport.x < 0 || viewport.y < 0 || viewport.x > 4096 || viewport.y > 4096 ||
        viewport.width - 2 * gap < 3 || spans.size() > capacity)
        return false;
    std::array<PixelRect, capacity> next{};
    const int usable = viewport.width - 2 * gap;
    int units = 0;
    auto boundary = [&](int u) { return usable * u / 3 + gap * u; };
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (spans[i] < 1 || spans[i] > 3)
            return false;
        const int end = units + static_cast<int>(spans[i]);
        next[i] = {boundary(units), 0, boundary(end) - gap - boundary(units), viewport.height};
        units = end;
    }
    viewport_ = viewport;
    tiles_ = next;
    size_ = spans.size();
    return true;
}
int RibbonLayout::maximum_offset() const noexcept {
    return size_ ? std::max(0, tiles_[size_ - 1].right() - viewport_.width) : 0;
}
int RibbonLayout::anchor(std::size_t index) const noexcept {
    return index < size_ ? std::min(tiles_[index].x, maximum_offset()) : 0;
}
bool RibbonController::configure(const RibbonLayout &layout, std::uint64_t now) noexcept {
    // Reconfiguration preserves position, gesture ownership and target. Each
    // item keeps a base rectangle, ready for per-tile motion in a later step.
    bool appended = layout.viewport() == layout_.viewport() && layout.size() >= layout_.size();
    for (std::size_t i = 0; appended && i < layout_.size(); ++i)
        appended = layout.tile(i) == layout_.tile(i);
    // Appending does not change any existing geometry or motion state.
    if (appended) {
        layout_ = layout;
        return true;
    }
    const double current = offset(now), previous_target = target_;
    layout_ = layout;
    selected_ = std::min(selected_, layout.size() ? layout.size() - 1 : 0);
    const double limit = layout.maximum_offset();
    from_ = std::clamp(current, 0.0, limit);
    target_ = std::clamp(previous_target, 0.0, limit);
    drag_offset_ = std::clamp(drag_offset_, 0.0, limit);
    release_velocity_ = 0;
    started_ = now;
    if (grabbed_ && *grabbed_ >= layout.size())
        grabbed_.reset();
    return true;
}
double RibbonController::offset(std::uint64_t now) const noexcept {
    if (held_ && horizontal_)
        return drag_offset_;
    if (now >= started_ && now - started_ >= duration)
        return target_;
    // Closed-form damped spring: independent of frame rate, no integration loop.
    // Velocity is in pixels/ms. The finite tail differs by less than a pixel.
    const double t = now <= started_ ? 0 : static_cast<double>(now - started_) / 1000;
    constexpr double decay = 18, frequency = 12;
    const double displacement = from_ - target_;
    return target_ + std::exp(-decay * t) * (displacement * std::cos(frequency * t) +
                                             (release_velocity_ * 1000 + decay * displacement) / frequency *
                                                 std::sin(frequency * t));
}
bool RibbonController::animating(std::uint64_t now) const noexcept {
    return !(held_ && horizontal_) && (from_ != target_ || release_velocity_ != 0) &&
           (now < started_ || now - started_ < duration);
}

bool RibbonController::select(std::size_t tile, std::uint64_t now, bool animate) noexcept {
    if (tile >= layout_.size())
        return false;
    const double current = offset(now);
    held_ = horizontal_ = rejected_ = false;
    grabbed_.reset();
    selected_ = tile;
    target_ = layout_.anchor(tile);
    from_ = animate ? current : target_;
    release_velocity_ = 0;
    started_ = last_interaction_ = now;
    return true;
}
void RibbonController::settle(std::uint64_t now, bool fling) noexcept {
    const double current = offset(now);
    // Project a short, bounded coast; slow releases choose the nearest anchor.
    const bool deliberate =
        paging_ == Paging::Free || std::abs(static_cast<std::int64_t>(last_x_) - down_x_) >= 24;
    const bool fresh = fling && deliberate && now >= last_move_ && now - last_move_ <= 120;
    const double speed = fresh ? std::clamp(-velocity_, -2.0, 2.0) : 0;
    const double projected =
        std::clamp(current + speed * 120, 0.0, static_cast<double>(layout_.maximum_offset()));
    std::size_t closest = 0;
    for (std::size_t i = 1; i < layout_.size(); ++i)
        if (std::abs(layout_.anchor(i) - projected) < std::abs(layout_.anchor(closest) - projected))
            closest = i;
    if (paging_ == Paging::Adjacent && layout_.size()) {
        const auto origin = std::min(gesture_origin_, layout_.size() - 1);
        const auto lower = origin ? origin - 1 : 0;
        const auto upper = std::min(origin + 1, layout_.size() - 1);
        closest = std::clamp(closest, lower, upper);
    }
    // An outward release at an edge starts the return immediately.
    release_velocity_ = (current < 0 || current > layout_.maximum_offset()) ? 0 : speed;
    // Capture the dragged position before releasing ownership of the gesture.
    held_ = false;
    grabbed_.reset();
    from_ = current;
    selected_ = closest;
    target_ = layout_.anchor(closest);
    started_ = last_interaction_ = now;
    horizontal_ = rejected_ = false;
}
void RibbonController::pointer(Event event, int x, int y, std::uint64_t now) noexcept {
    if (event == Event::Down) {
        const auto v = layout_.viewport();
        if (x < v.x || x >= v.right() || y < v.y || y >= v.bottom() || !layout_.size())
            return;
        const double current = offset(now);
        // A grabbed transition starts at the nearest visible page, not its old target.
        gesture_origin_ = 0;
        for (std::size_t i = 1; i < layout_.size(); ++i)
            if (std::abs(layout_.anchor(i) - current) < std::abs(layout_.anchor(gesture_origin_) - current))
                gesture_origin_ = i;
        grabbed_.reset();
        for (std::size_t i = 0; i < layout_.size(); ++i) {
            const auto tile = layout_.tile(i);
            const double point = x - v.x + current;
            if (point >= tile.x && point < tile.right()) {
                grabbed_ = i;
                break;
            }
        }
        held_ = true;
        horizontal_ = rejected_ = false;
        velocity_ = 0;
        down_x_ = last_x_ = x;
        down_y_ = y;
        last_move_ = last_interaction_ = now;
        release_velocity_ = 0;
        drag_offset_ = from_ = target_ = current;
        // Recover the unconstrained gesture coordinate before applying resistance
        // again. Capturing the displayed position would apply resistance twice.
        const double edge = std::clamp(current, 0.0, static_cast<double>(layout_.maximum_offset()));
        const double excess = current - edge;
        down_offset_ = edge + excess * edge_travel / std::max(.001, edge_travel - std::abs(excess));
        return;
    }
    if (!held_)
        return;
    if (event == Event::Move || event == Event::Up) {
        const auto dx = static_cast<std::int64_t>(x) - down_x_, dy = static_cast<std::int64_t>(y) - down_y_;
        if (!horizontal_ && !rejected_ && std::max(std::abs(dx), std::abs(dy)) >= 8) {
            horizontal_ = std::abs(dx) > std::abs(dy);
            rejected_ = !horizontal_;
        }
        if (horizontal_) {
            if (now > last_move_ && x != last_x_)
                velocity_ = (static_cast<double>(x) - last_x_) / (now - last_move_);
            const double raw = down_offset_ - dx, limit = layout_.maximum_offset();
            // Bounded rubber-band travel; the derivative decreases with distance.
            const double edge = std::clamp(raw, 0.0, limit), excess = raw - edge;
            drag_offset_ = edge + excess / (1 + std::abs(excess) / edge_travel);
        }
        if (x != last_x_)
            last_move_ = now;
        last_x_ = x;
        last_interaction_ = now;
    }
    if (event == Event::Up || event == Event::Cancel)
        settle(now, event == Event::Up && horizontal_);
}
std::optional<std::uint64_t> RibbonController::idle_deadline() const noexcept {
    if (!idle_timeout_ || held_ || target_ == 0)
        return {};
    return last_interaction_ > std::numeric_limits<std::uint64_t>::max() - idle_timeout_
               ? std::numeric_limits<std::uint64_t>::max()
               : last_interaction_ + idle_timeout_;
}
void RibbonController::update(std::uint64_t now) noexcept {
    if (const auto deadline = idle_deadline(); deadline && now >= *deadline)
        select(0, now);
}
} // namespace ui
