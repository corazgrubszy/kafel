#pragma once
#include <cstddef>
// Benchmark-only instrumentation; not part of the UI API.
namespace ui::detail {
struct SvgMemoryStats {
    std::size_t live, peak, allocations;
};
SvgMemoryStats svg_memory_stats() noexcept;
} // namespace ui::detail
