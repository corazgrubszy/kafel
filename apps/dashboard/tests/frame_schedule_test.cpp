#include "app/frame_schedule.hpp"
#include "dashboard/dashboard_view.hpp"
#include <array>
#include <cstdio>
void check(bool, const char *);
void test_frame_schedule() {
    using namespace kafel;
    app::FrameSchedule schedule;
    check(schedule.frame_due(0, true), "first frame is immediate");
    schedule.presented(0);
    check(!schedule.frame_due(39, true) && schedule.frame_due(40, true), "frames capped at 25 fps");
    check(!schedule.frame_due(1000, false), "idle never redraws just because time passed");
    check(schedule.wait_ms(10, false, 1000, {}) == 990, "idle sleeps until sampling");
    check(schedule.wait_ms(10, true, 1000, {}) == 30, "animation waits until next frame");
    check(schedule.wait_ms(10, false, 1000, 25) == 15, "navigation deadline wakes before sampling");
    check(schedule.wait_ms(1001, false, 1000, {}) == 0, "overdue work never sleeps");
    std::puts("PASS: poll scheduling and 25 fps frame deadlines");
}
