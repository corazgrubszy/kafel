#include "memory.hpp"
#include <cstdlib>
#include <limits>
#include <new>
namespace ui_test {
namespace {
Memory counters;
struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) Header {
    std::size_t size;
};
static_assert(alignof(Header) >= alignof(std::max_align_t));
} // namespace
Memory memory() noexcept { return counters; }
void *allocate(std::size_t size) noexcept {
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(Header))
        return nullptr;
    auto *header = static_cast<Header *>(std::malloc(sizeof(Header) + size));
    if (!header)
        return nullptr;
    header->size = size;
    counters.live += size;
    ++counters.allocations;
    if (counters.live > counters.peak)
        counters.peak = counters.live;
    return header + 1;
}
void release(void *pointer) noexcept {
    if (!pointer)
        return;
    auto *header = static_cast<Header *>(pointer) - 1;
    counters.live -= header->size;
    std::free(header);
}
} // namespace ui_test
void *operator new(std::size_t size) {
    if (auto *p = ui_test::allocate(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { ui_test::release(p); }
void operator delete[](void *p) noexcept { ui_test::release(p); }
void operator delete(void *p, std::size_t) noexcept { ui_test::release(p); }
void operator delete[](void *p, std::size_t) noexcept { ui_test::release(p); }
