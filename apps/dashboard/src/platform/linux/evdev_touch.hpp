#pragma once
#include "input/event_timeline.hpp"
#include "input/touch_decoder.hpp"

namespace kafel::linux_device {
// Startup discovery and nonblocking reads. No allocations in pump().
// Direct ABS_X/Y + BTN_TOUCH compatibility is required; keyboards are rejected.
class EvdevTouch {
  public:
    using Sink = void (*)(void *, TouchSample, std::uint64_t);
    EvdevTouch(int width, int height, const char *path = nullptr);
    ~EvdevTouch();
    EvdevTouch(const EvdevTouch &) = delete;
    EvdevTouch &operator=(const EvdevTouch &) = delete;
    int descriptor() const noexcept { return fd_; } // Borrowed, owned by this object.
    void pump(Sink sink, void *data, std::uint64_t now_ms);

  private:
    int fd_ = -1;
    std::optional<TouchDecoder> decoder_;
    bool dropped_ = false;
    EventTimeline timeline_;
    bool accept(const char *path, int width, int height);
    void resync();
};
} // namespace kafel::linux_device
