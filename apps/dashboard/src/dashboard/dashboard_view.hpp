#pragma once
#include "clock_painter.hpp"
#include "dash5/state.hpp"
#include "graphics/surface.hpp"
#include "input/touch_decoder.hpp"
#include "service_card.hpp"
#include "system/system_snapshot.hpp"
#include "ui/card.hpp"
#include "ui/ribbon.hpp"
#include "ui/tile.hpp"
#include <array>
#include <optional>
#include <string>
namespace kafel {
class DashboardView {
  public:
    explicit DashboardView(std::uint64_t idle_timeout_ms = 60000);
    DashboardView(const DashboardView &) = delete;
    DashboardView &operator=(const DashboardView &) = delete;
    void update_system(const SystemSnapshot &) noexcept;
    // Every input must be a checked card, never a moved-from source.
    bool replace_services(std::span<const ServiceCard>, std::uint64_t);
    bool update_clock(ClockTime, std::uint64_t) noexcept;
    bool update_date(unsigned, unsigned, unsigned, unsigned) noexcept;
    void pointer(TouchSample, std::uint64_t) noexcept;
    void update(std::uint64_t) noexcept;
    bool needs_redraw(std::uint64_t) const noexcept;
    std::optional<std::uint64_t> next_visual_change(std::uint64_t) const noexcept;
    auto idle_deadline() const noexcept { return ribbon_.idle_deadline(); }
    bool render(Rgb565Surface &, std::uint64_t);
    bool has_attention() const noexcept;
    enum class ServiceHealth { Empty, Ok, Warning, Error };
    ServiceHealth service_health() const noexcept;
    std::size_t selected_tile() const noexcept { return ribbon_.selected(); }
    std::size_t service_count() const noexcept { return services_.size(); }
    const ui::RibbonController &ribbon() const noexcept { return ribbon_; }

  private:
    std::vector<ServiceCard> services_;
    SystemSnapshot snapshot_;
    FixedText<6> time_, previous_time_;
    FixedText<65> date_;
    std::uint64_t clock_started_ = 0;
    bool clock_initialized_ = false, clock_animating_ = false, dirty_ = true, last_animation_ = false;
    int painted_position_ = 0;
    ui::TextFont text_, headings_, failure_text_;
    ui::NumberFont numbers_;
    ui::TilePainter painter_;
    ClockPainter clock_painter_;
    ui::RibbonController ribbon_;
    std::array<unsigned char, 240 * 320 * 3> rgb_{};
    void rebuild(std::uint64_t) noexcept;
    bool clock_active(std::uint64_t) const noexcept;
    std::shared_ptr<const ui::CardFrame> marquee_frame_;
    std::size_t marquee_page_ = 0;
    std::uint64_t marquee_started_ = 0, rendered_at_ = 0;
    const ServiceCard *visible_service(std::uint64_t) const noexcept;
    void draw_service(ui::Surface &, int, const ServiceCard &, std::uint64_t);
};
} // namespace kafel
