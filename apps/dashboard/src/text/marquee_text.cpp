#include "text/marquee_text.hpp"

#include <algorithm>
#include <climits>

namespace kafel {
int MarqueeText::offset(int text_width, int viewport_width, std::uint64_t elapsed_ms) noexcept {
    if (viewport_width <= 0 || text_width <= viewport_width) return 0;
    const auto distance = static_cast<std::uint64_t>(text_width - viewport_width);
    const auto travel = (distance * 1000 + pixels_per_second - 1) / pixels_per_second;
    auto phase = elapsed_ms % (2 * (pause_ms + travel));
    if (phase < pause_ms) return 0;
    phase -= pause_ms;
    if (phase < travel) return static_cast<int>(phase * distance / travel);
    phase -= travel;
    if (phase < pause_ms) return static_cast<int>(distance);
    phase -= pause_ms;
    return static_cast<int>(distance - phase * distance / travel);
}
std::optional<std::uint64_t> MarqueeText::next_change(int width,int viewport,std::uint64_t elapsed) noexcept {
    if(viewport<=0 || width<=viewport) return {};
    const auto distance=static_cast<std::uint64_t>(width-viewport);
    const auto travel=(distance*1000+pixels_per_second-1)/pixels_per_second;
    const auto phase=elapsed%(2*(pause_ms+travel));
    if(phase<pause_ms) return pause_ms-phase+(travel+distance-1)/distance;
    if(phase<pause_ms+travel) {
        const auto moving=phase-pause_ms;
        return ((moving*distance/travel+1)*travel+distance-1)/distance-moving;
    }
    if(phase<2*pause_ms+travel) return 2*pause_ms+travel-phase+(travel+distance-1)/distance;
    const auto moving=phase-(2*pause_ms+travel);
    return ((moving*distance/travel+1)*travel+distance-1)/distance-moving;
}
}
