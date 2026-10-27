#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace kafel {
struct TouchSample {
    enum class Kind { Down, Move, Up, Cancel };
    Kind kind;
    int x, y;
};
struct TouchAxis {
    int minimum, maximum;
};

// Converts the single-pointer compatibility stream of a direct touchscreen.
// Emits only on report boundaries. MT slots detect extra fingers so a gesture
// cannot jump between fingers. The Linux adapter owns I/O and loss recovery.
class TouchDecoder {
  public:
    enum class Event { X, Y, Contact, Slot, TrackingId, Report };
    static constexpr int max_slots = 16;
    TouchDecoder(TouchAxis x, TouchAxis y, int width, int height);
    std::optional<TouchSample> feed(Event event, int value) noexcept;
    std::optional<TouchSample> cancel() noexcept;
    void reset(bool held, int raw_x, int raw_y) noexcept;
    static int coordinate(int raw, TouchAxis axis, int extent) noexcept;

  private:
    TouchAxis x_axis_, y_axis_;
    int width_, height_, raw_x_, raw_y_, x_ = 0, y_ = 0, slot_ = 0;
    std::array<bool, max_slots> slots_{};
    unsigned contacts_ = 0;
    bool contact_ = false, down_ = false, blocked_ = false;
};
} // namespace kafel
