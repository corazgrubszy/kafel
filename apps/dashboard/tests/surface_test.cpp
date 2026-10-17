#include "graphics/surface.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <limits>
#include <stdexcept>

void check(bool condition, const char* message);

void test_surface() {
    using namespace kafel;
    check(rgb(255, 0, 0) == 0xf800 && rgb(0, 255, 0) == 0x07e0 &&
          rgb(0, 0, 255) == 0x001f && rgb(255, 255, 255) == 0xffff, "RGB565 channels");
    // Guard pixels before/after the view and padding at the end of each row.
    std::array<Pixel, 17> storage;
    storage.fill(0xbeef);
    Surface s(std::span<Pixel>(storage).subspan(1, 15), 3, 3, 5);
    s.clear(0);
    s.fill_rect({-10, -10, 2, 2}, 7);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x)
        check(storage[1 + y * 5 + x] == (x < 2 && y < 2 ? 7 : 0), "screen clipping");
    s.set_clip({1, 1, 2, 3});
    s.fill_rect({INT_MIN, INT_MIN, INT_MAX, INT_MAX}, 9);
    check(storage[7] == 9 && storage[12] == 9 && storage[6] == 7, "custom clip and extreme bounds");
    s.fill_rect({2, 2, 1, 1}, 42);
    check(storage[12] == 9, "reversed rectangle is empty");
    s.clear(3);
    s.fill_rect({0, 0, 3, 3}, 4);
    check(storage[1] == 3 && storage[7] == 4, "clear ignores but preserves clip");
    s.set_clip({10, 10, 20, 20});
    s.fill_rect({INT_MIN, INT_MIN, INT_MAX, INT_MAX}, 55);
    check(storage[7] == 4, "offscreen clip is empty");
    s.reset_clip();
    s.fill_rect({2, 2, 3, 3}, 6);
    check(storage[13] == 6 && storage[12] == 4, "single pixel and exclusive right edge");
    check(storage.front() == 0xbeef && storage.back() == 0xbeef, "buffer guards");
    for (int y = 0; y < 3; ++y)
        check(storage[1 + y * 5 + 3] == 0xbeef && storage[1 + y * 5 + 4] == 0xbeef, "stride padding");
    const auto rejects = [&](int w, int h, std::size_t stride) {
        try { Surface invalid(storage, w, h, stride); }
        catch (const std::invalid_argument&) { return true; }
        return false;
    };
    check(rejects(0, 1, 1) && rejects(-1, 1, 1) && rejects(1, 0, 1), "invalid dimensions");
    check(rejects(3, 2, 2) && rejects(1, 1, 0), "invalid stride");
    check(rejects(3, 6, 3) && rejects(1, 2, std::numeric_limits<std::size_t>::max()), "short buffer and overflow");
    std::puts("PASS: renderer colors, clipping, stride, guards and validation");
}
