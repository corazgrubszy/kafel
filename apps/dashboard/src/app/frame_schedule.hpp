#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace kafel::app {
// Monotonic deadlines only. No sleeping or OS dependency, so policy is testable.
class FrameSchedule {
public:
    bool frame_due(std::uint64_t now, bool dirty) const noexcept {
        return dirty && (!presented_ || now>=next_frame_);
    }
    void presented(std::uint64_t now) noexcept { presented_=true; next_frame_=add(now,40); }
    int wait_ms(std::uint64_t now,bool dirty,std::uint64_t sample,
                std::optional<std::uint64_t> idle,std::optional<std::uint64_t> visual={}) const noexcept {
        auto deadline=sample;
        if(visual) deadline=std::min(deadline,*visual);
        if(idle) deadline=std::min(deadline,*idle);
        if(dirty) deadline=std::min(deadline,presented_?next_frame_:now);
        return deadline<=now?0:static_cast<int>(std::min<std::uint64_t>(deadline-now,std::numeric_limits<int>::max()));
    }
private:
    bool presented_=false;
    std::uint64_t next_frame_=0;
    static std::uint64_t add(std::uint64_t a,std::uint64_t b) noexcept {
        return a>std::numeric_limits<std::uint64_t>::max()-b?std::numeric_limits<std::uint64_t>::max():a+b;
    }
};
}
