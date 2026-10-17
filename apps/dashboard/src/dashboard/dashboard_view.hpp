#pragma once
#include "graphics/surface.hpp"
#include "input/touch_decoder.hpp"
#include "services/service_registry.hpp"
#include "system/system_snapshot.hpp"
#include "ui/tile.hpp"
#include "ui/ribbon.hpp"
#include <array>
#include <optional>
namespace kafel {
// Application adapter: copies live data, owns resources, chooses tile content.
// Geometry/navigation and all tile styling remain in ui; Linux stays in app.
class DashboardView {
public:
    explicit DashboardView(std::uint64_t idle_timeout_ms=60000);
    DashboardView(const DashboardView&)=delete;
    DashboardView& operator=(const DashboardView&)=delete;
    void update_system(const SystemSnapshot& snapshot) noexcept;
    bool replace_services(std::span<const ServiceTileStatus> tiles,std::uint64_t now_ms) noexcept;
    bool update_clock(ClockTime time,std::uint64_t now_ms) noexcept;
    bool update_date(unsigned year,unsigned month,unsigned day,unsigned weekday) noexcept;
    void pointer(TouchSample sample,std::uint64_t now_ms) noexcept;
    void update(std::uint64_t now_ms) noexcept;
    bool needs_redraw(std::uint64_t now_ms) const noexcept;
    std::optional<std::uint64_t> next_visual_change(std::uint64_t now_ms) const noexcept;
    auto idle_deadline() const noexcept { return ribbon_.idle_deadline(); }
    bool render(Surface& output,std::uint64_t now_ms);
    bool has_attention() const noexcept;
    std::size_t selected_tile() const noexcept { return ribbon_.selected(); }
    std::size_t service_count() const noexcept { return count_; }
    const ui::RibbonController& ribbon() const noexcept { return ribbon_; }
private:
    struct Service {
        FixedText<33> service_id,tile_id;
        unsigned span=3;
        int width=232;
        FixedText<49> name;
        FixedText<service_detail_bytes+1> detail;
        FixedText<16> status;
        bool alarm=false,attention=false;
        std::optional<MetricMessage> metric;
        std::array<int,metric_caption_capacity> caption_widths{};
        std::uint64_t caption_started=0,marquee_started=0;
        int detail_width=0,detail_caption_width=0,name_width=0,status_width=0;
    };
    struct ScrollOffsets { int name=0,detail=0; unsigned caption=0,alpha=255; int detail_caption=0; bool operator==(const ScrollOffsets&) const=default; };
    std::array<Service,service_tile_capacity> services_{};
    std::size_t count_=0;
    SystemSnapshot snapshot_;
    FixedText<6> time_,previous_time_;
    FixedText<65> date_;
    std::uint64_t clock_started_=0;
    bool clock_initialized_=false,clock_animating_=false,dirty_=true,last_animation_=false;
    int painted_position_=0;
    std::array<ScrollOffsets,service_tile_capacity> painted_scroll_{};
    ui::TextFont text_;
    ui::NumberFont numbers_;
    ui::TilePainter painter_;
    ui::RibbonController ribbon_;
    std::array<unsigned char,240*320*3> rgb_{};
    static constexpr ui::PixelRect viewport{4,240,232,76};
    void assign_service(Service&,const ServiceTileStatus&,std::uint64_t now);
    static bool same_service(const Service&,const ServiceTileStatus&) noexcept;
    static bool valid_service(const ServiceStatus&) noexcept;
    static bool compact_header(const Service& service) noexcept;
    static int name_viewport(const Service& service) noexcept;
    bool clock_active(std::uint64_t now) const noexcept;
    std::uint64_t marquee_time(const Service&,std::uint64_t now) const noexcept;
    static std::uint64_t caption_period(const Service&) noexcept;
    std::uint64_t caption_time(const Service&,std::uint64_t now) const noexcept;
    ScrollOffsets scroll_offsets(std::size_t index,std::uint64_t now) const noexcept;
    bool visible(std::size_t service,std::uint64_t now) const noexcept;
    void draw_service(ui::Surface& surface,ui::PixelRect bounds,const Service& service,ScrollOffsets offsets);
    int rich_width(std::string_view text,bool small=false) const;
    void rich_draw(ui::Surface&,ui::PixelRect clip,int x,int y,std::string_view text,bool small=false) const;
    void rebuild_ribbon(std::uint64_t now_ms) noexcept;
};
}
