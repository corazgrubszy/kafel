#include "dashboard/dashboard_view.hpp"
#include "input/touch_decoder.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <stdexcept>

void check(bool, const char *);
void test_touch() {
    using namespace kafel;
    using E = TouchDecoder::Event;
    using K = TouchSample::Kind;
    TouchDecoder touch({0, 239}, {0, 319}, 240, 320);
    check(!touch.feed(E::Contact, 1) && !touch.feed(E::Y, 260) && !touch.feed(E::X, 120),
          "contact and coordinates wait for SYN_REPORT");
    auto sample = touch.feed(E::Report, 0);
    check(sample && sample->kind == K::Down && sample->x == 120 && sample->y == 260,
          "Goodix report becomes one coherent pointer-down");
    touch.feed(E::X, 121);
    touch.feed(E::X, 122);
    touch.feed(E::Y, 140);
    sample = touch.feed(E::Report, 0);
    check(sample && sample->kind == K::Move && sample->x == 122 && sample->y == 140,
          "multiple axis updates coalesce to final coordinates");
    check(!touch.feed(E::Report, 0), "stationary reports do not emit duplicate moves");
    touch.feed(E::Y, 130);
    touch.feed(E::Contact, 0);
    sample = touch.feed(E::Report, 0);
    check(sample && sample->kind == K::Up && sample->y == 130, "release retains final coordinates");
    check(!touch.feed(E::Report, 0), "release is emitted once");
    touch.feed(E::X, INT_MAX);
    touch.feed(E::Y, INT_MIN);
    touch.feed(E::Contact, 1);
    sample = touch.feed(E::Report, 0);
    check(sample && sample->x == 239 && sample->y == 0, "out-of-range raw coordinates clamp to screen");
    check(TouchDecoder::coordinate(100, {100, 1100}, 240) == 0 &&
              TouchDecoder::coordinate(1100, {100, 1100}, 240) == 239 &&
              TouchDecoder::coordinate(600, {100, 1100}, 240) == 119,
          "axis scaling includes both endpoints and nonzero origin");
    check(TouchDecoder::coordinate(INT_MAX, {INT_MIN, INT_MAX}, 4096) == 4095,
          "full signed axis range does not overflow");
    sample = touch.cancel();
    check(sample && sample->kind == K::Cancel && !touch.cancel(), "loss cancellation emits exactly once");
    touch.reset(true, 120, 200);
    touch.feed(E::Y, 100);
    check(!touch.feed(E::Report, 0), "held finger after startup/loss cannot start phantom gesture");
    touch.feed(E::Contact, 0);
    check(!touch.feed(E::Report, 0), "release unblocks recovery without extra up");
    touch.feed(E::Contact, 1);
    sample = touch.feed(E::Report, 0);
    check(sample && sample->kind == K::Down, "fresh contact after loss works");
    touch.feed(E::Slot, 0);
    touch.feed(E::TrackingId, 10);
    touch.feed(E::Report, 0);
    touch.feed(E::Slot, 1);
    touch.feed(E::TrackingId, 11);
    sample = touch.feed(E::Report, 0);
    check(sample && sample->kind == K::Cancel, "second finger cancels gesture rather than switching pointer");
    touch.feed(E::Slot, 1);
    touch.feed(E::TrackingId, -1);
    touch.feed(E::X, 10);
    check(!touch.feed(E::Report, 0), "one remaining finger stays blocked until all fingers lift");
    touch.feed(E::Slot, 0);
    touch.feed(E::TrackingId, -1);
    touch.feed(E::Contact, 0);
    touch.feed(E::Report, 0);
    touch.feed(E::Slot, 0);
    touch.feed(E::TrackingId, 12);
    touch.feed(E::Contact, 1);
    check(touch.feed(E::Report, 0)->kind == K::Down, "multitouch cancellation recovers on fresh press");
    touch.feed(E::Slot, 99);
    sample = touch.feed(E::Report, 0);
    check(sample && sample->kind == K::Cancel, "invalid MT slot cancels safely");
    bool rejected = false;
    try {
        TouchDecoder invalid({1, 1}, {0, 10}, 240, 320);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "invalid axis geometry rejected");

    // Decoded reports reach the current production ribbon.
    DashboardView view;
    const std::array<ServiceCard, 1> services = {
        {ServiceCard({"probe", "one", "Probe", dash5::TileState::Error, "Brak danych", false, false, {}})}};
    view.replace_services(services, 0);
    std::array<Pixel, 240 * 320> pixels{};
    Rgb565Surface surface(pixels, 240, 320, 240);
    view.render(surface, 0);
    touch.reset(false, 210, 80);
    touch.feed(E::Contact, 1);
    sample = touch.feed(E::Report, 0);
    view.pointer(*sample, 100);
    touch.feed(E::X, 10);
    touch.feed(E::Contact, 0);
    sample = touch.feed(E::Report, 0);
    view.pointer(*sample, 200);
    view.render(surface, 900);
    check(view.ribbon().offset(900) > 0, "decoded release updates the production ribbon safely");
    std::puts("PASS: evdev decoding, bounds, multitouch and recovery");
}
