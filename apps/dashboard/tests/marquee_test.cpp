#include "ui/marquee.hpp"
#include <array>
#include <climits>
#include <cstdio>

void check(bool, const char *);
void test_marquee() {
    using ui::MarqueeText;
    check(MarqueeText::offset(100, 100, 9999) == 0 && MarqueeText::offset(90, 100, 9999) == 0,
          "fitting text stays stationary");
    check(MarqueeText::offset(124, 100, 1199) == 0, "initial reading pause");
    check(MarqueeText::offset(124, 100, 1700) == 12, "24 pixels per second outward");
    check(MarqueeText::offset(124, 100, 2200) == 24 && MarqueeText::offset(124, 100, 3399) == 24,
          "pause at end exposes final characters");
    check(MarqueeText::offset(124, 100, 3900) == 12 && MarqueeText::offset(124, 100, 4400) == 0,
          "return and repeat without jump");
    check(MarqueeText::offset(INT_MAX, 1, UINT64_MAX) >= 0 && MarqueeText::offset(20, 0, 2000) == 0,
          "large clocks and empty viewports are safe");
    check(!MarqueeText::next_change(100, 100, 0), "fitting text has no wake deadline");
    check(MarqueeText::next_change(124, 100, 0) == 1242, "initial pause wakes at first actual pixel change");
    check(MarqueeText::next_change(124, 100, 2200) == 1242, "end pause wakes at first returning pixel");
    for (int distance : {1, 24, 37, 500})
        for (std::uint64_t now = 0; now < 10000; now += 17) {
            const auto next = *MarqueeText::next_change(100 + distance, 100, now);
            const int before = MarqueeText::offset(100 + distance, 100, now);
            check(next > 0 && MarqueeText::offset(100 + distance, 100, now + next) != before,
                  "deadline identifies next changed pixel");
            check(next == 1 || MarqueeText::offset(100 + distance, 100, now + next - 1) == before,
                  "no earlier pixel change before deadline");
        }
    std::puts("PASS: marquee offsets, pauses and exact wake deadlines");
}
