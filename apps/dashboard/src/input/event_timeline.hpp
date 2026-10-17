#pragma once
#include <algorithm>
#include <cstdint>
namespace kafel {
// Translate CLOCK_MONOTONIC input timestamps into application-relative time.
// Old startup events map to zero; malformed/future or regressing times cannot
// move the UI clock backwards or beyond the current pump.
class EventTimeline {
public:
    std::uint64_t map(std::uint64_t event,std::uint64_t kernel_now,std::uint64_t app_now) noexcept {
        const auto age=kernel_now>event?kernel_now-event:0;
        const auto mapped=age>app_now?0:app_now-age;
        last_=std::min(app_now,std::max(last_,mapped));
        return last_;
    }
private:
    std::uint64_t last_=0;
};
}
