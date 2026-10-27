#include "dashboard/dashboard_view.hpp"
#include <array>
#include <cstdlib>
#include <new>
namespace {
bool recording = false;
std::size_t allocations = 0;
thread_local std::ptrdiff_t failure_after = -1;
thread_local bool allocation_failed = false;
} // namespace
void fail_allocation_after(std::size_t count) {
    failure_after = static_cast<std::ptrdiff_t>(count);
    allocation_failed = false;
}
bool stop_allocation_failure() {
    failure_after = -1;
    return allocation_failed;
}
void *operator new(std::size_t size) {
    if (failure_after >= 0 && failure_after-- == 0) {
        failure_after = -1;
        allocation_failed = true;
        throw std::bad_alloc();
    }
    if (recording)
        ++allocations;
    if (auto *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
void check(bool, const char *);
template <class Change> kafel::ServiceCard changed(const kafel::ServiceCard &card, Change change) {
    auto fields = card.data();
    change(fields);
    return kafel::ServiceCard(std::move(fields));
}
void test_allocations() {
    using namespace kafel;
    DashboardView view;
    SystemSnapshot system;
    system.hostname.assign("kafel");
    view.update_system(system);
    view.update_clock({16, 35}, 0);
    view.update_date(2026, 10, 17, 6);
    std::vector<ui::MarqueeLayer> layers{{{12, 208, 216, 20},
                                          12,
                                          210,
                                          400,
                                          16,
                                          184,
                                          {255, 255, 255},
                                          std::vector<unsigned char>(400 * 16, 255)}};
    auto frame = std::make_shared<ui::CardFrame>(std::vector<unsigned char>(240 * 240 * 3, 80), 0,
                                                 std::move(layers), 0, 4);
    const std::array<ServiceCard, 1> cards = {
        {ServiceCard({"test", "one", "Test", dash5::TileState::Ok, "", false, false, frame})}};
    view.replace_services(cards, 0);
    std::array<Pixel, 240 * 320> pixels{};
    Rgb565Surface output(pixels, 240, 320, 240);
    bool valid = true;
    view.render(output, 0);
    allocations = 0;
    recording = true;
    view.pointer({TouchSample::Kind::Down, 210, 100}, 100);
    for (unsigned i = 0; i < 20; ++i) {
        view.pointer({TouchSample::Kind::Move, 210 - int(i) * 10, 100}, 120 + i * 20);
        valid &= view.render(output, 120 + i * 20);
    }
    view.pointer({TouchSample::Kind::Up, 10, 100}, 600);
    for (unsigned now = 600; now < 14000; now += 40)
        valid &= view.render(output, now);
    recording = false;
    const auto gesture_allocations = allocations;
    for (bool offline : {false, true}) {
        auto failed = cards;
        failed[0] = changed(failed[0], [&](auto &f) {
            f.state = offline ? dash5::TileState::Ok : dash5::TileState::Error;
            f.message = offline ? "" : "Brak danych";
            f.offline = offline;
        });
        view.replace_services(failed, 14000);
        allocations = 0;
        recording = true;
        valid &= view.render(output, 14000);
        recording = false;
        check(valid && allocations == 0,
              "failure/offline painting uses only prepared fonts and no allocations");
    }
    allocations = gesture_allocations;
    check(valid && allocations == 0,
          "pager, bouncing text, pauses and cached service painting allocate zero C++ memory");
}
