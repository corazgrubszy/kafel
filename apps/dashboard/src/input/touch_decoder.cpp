#include "input/touch_decoder.hpp"
#include <algorithm>
#include <stdexcept>

namespace kafel {
TouchDecoder::TouchDecoder(TouchAxis x, TouchAxis y, int width, int height)
    : x_axis_(x), y_axis_(y), width_(width), height_(height), raw_x_(x.minimum), raw_y_(y.minimum) {
    if (x.maximum <= x.minimum || y.maximum <= y.minimum || width < 2 || height < 2 || width > 4096 ||
        height > 4096)
        throw std::invalid_argument("invalid touch axes or screen size");
}
int TouchDecoder::coordinate(int raw, TouchAxis axis, int extent) noexcept {
    if (axis.maximum <= axis.minimum || extent < 2)
        return 0;
    const auto clamped = std::clamp(raw, axis.minimum, axis.maximum);
    const auto numerator = (static_cast<std::int64_t>(clamped) - axis.minimum) * (extent - 1);
    return static_cast<int>(numerator / (static_cast<std::int64_t>(axis.maximum) - axis.minimum));
}
void TouchDecoder::reset(bool held, int raw_x, int raw_y) noexcept {
    raw_x_ = raw_x;
    raw_y_ = raw_y;
    x_ = coordinate(raw_x_, x_axis_, width_);
    y_ = coordinate(raw_y_, y_axis_, height_);
    down_ = false;
    contact_ = held;
    blocked_ = held;
    slots_ = {};
    contacts_ = 0;
    slot_ = 0;
}
std::optional<TouchSample> TouchDecoder::cancel() noexcept {
    const bool was_down = down_;
    down_ = false;
    blocked_ = true;
    if (was_down)
        return TouchSample{TouchSample::Kind::Cancel, x_, y_};
    return std::nullopt;
}
std::optional<TouchSample> TouchDecoder::feed(Event event, int value) noexcept {
    switch (event) {
    case Event::X:
        raw_x_ = value;
        return std::nullopt;
    case Event::Y:
        raw_y_ = value;
        return std::nullopt;
    case Event::Contact:
        contact_ = value != 0;
        if (!contact_) {
            slots_ = {};
            contacts_ = 0;
            slot_ = 0;
        }
        return std::nullopt;
    case Event::Slot:
        slot_ = value;
        if (slot_ < 0 || slot_ >= max_slots)
            blocked_ = true;
        return std::nullopt;
    case Event::TrackingId:
        if (slot_ >= 0 && slot_ < max_slots) {
            const bool present = value >= 0;
            auto &slot = slots_[static_cast<std::size_t>(slot_)];
            if (slot != present) {
                if (present)
                    ++contacts_;
                else
                    --contacts_;
                slot = present;
            }
            if (contacts_ > 1)
                blocked_ = true;
        }
        return std::nullopt;
    case Event::Report:
        break;
    }
    if (blocked_) {
        const bool was_down = down_;
        down_ = false;
        if (!contact_)
            blocked_ = false;
        if (was_down)
            return TouchSample{TouchSample::Kind::Cancel, x_, y_};
        return std::nullopt;
    }
    const int x = coordinate(raw_x_, x_axis_, width_), y = coordinate(raw_y_, y_axis_, height_);
    const bool moved = x != x_ || y != y_;
    x_ = x;
    y_ = y;
    if (contact_ && !down_) {
        down_ = true;
        return TouchSample{TouchSample::Kind::Down, x, y};
    }
    if (!contact_ && down_) {
        down_ = false;
        return TouchSample{TouchSample::Kind::Up, x, y};
    }
    if (contact_ && down_ && moved)
        return TouchSample{TouchSample::Kind::Move, x, y};
    return std::nullopt;
}
} // namespace kafel
