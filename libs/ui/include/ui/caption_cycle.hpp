#pragma once
#include <cstdint>
#include <algorithm>
#include <optional>
namespace ui {
// Stateless sampling: no timer, allocations or dependency on a frame loop.
class CaptionCycle {
public:
    static constexpr std::uint64_t period=4800,fade=160,frame=40;
    struct Sample {
        unsigned index=0,alpha=255;
        std::uint64_t elapsed=0;
        bool operator==(const Sample&) const=default;
    };
    static Sample sample(unsigned count,std::uint64_t elapsed,std::uint64_t interval=period) noexcept {
        if(count<2) return {0,255,elapsed};
        interval=std::max(interval,period);
        const auto phase=elapsed%interval;
        const auto step=phase/frame*frame;
        unsigned alpha=255;
        if(elapsed>=interval&&step<fade) alpha=static_cast<unsigned>(step*255/fade);
        else if(step>=interval-fade) alpha=static_cast<unsigned>((interval-step)*255/fade);
        return {static_cast<unsigned>((elapsed/interval)%count),alpha,phase};
    }
    // Relative deadline. Opacity changes at 25fps; plateaus sleep.
    static std::optional<std::uint64_t> next_change(unsigned count,std::uint64_t elapsed,std::uint64_t interval=period) noexcept {
        if(count<2) return {};
        interval=std::max(interval,period);
        const auto phase=elapsed%interval;
        if((elapsed>=interval&&phase<fade)||phase>=interval-fade) return frame-phase%frame;
        return interval-fade-phase;
    }
};
}
