#pragma once
#include <cstddef>
namespace ui_test {
struct Memory {
    std::size_t live = 0, peak = 0, allocations = 0;
};
Memory memory() noexcept;
void *allocate(std::size_t size) noexcept;
void release(void *pointer) noexcept;
} // namespace ui_test
