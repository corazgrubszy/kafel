#include "dashboard/dashboard_view.hpp"
#include "dashboard/large_tile_layout.hpp"
#include "dashboard/theme.hpp"
#include "system/system_metrics.hpp"
#include "text/utf8.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace kafel {
namespace {
extern "C" const unsigned char *ui_sans_regular_data();
extern "C" std::size_t ui_sans_regular_size();
extern "C" const unsigned char *ui_sans_bold_data();
extern "C" std::size_t ui_sans_bold_size();
constexpr auto &theme = kafel::rose_pine_moon;
constexpr std::array text_styles = {theme.eyebrow,       theme.secondary,      theme.date,
                                    theme.label,         theme.unit,           theme.service_name,
                                    theme.service_state, theme.service_detail, theme.attention};
constexpr ui::TextStyle failure_style{kafel::LargeTileLayout::failure_value_size, theme.error.accent, 0};
constexpr std::array number_styles = {theme.clock_number, theme.metric_number, theme.metric_number_compact};
constexpr std::string_view repertoire =
    " !\"#$%&'()*+,-./"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ąćęłńóśźżĄĆĘŁŃÓŚŹŻ°–—";
std::span<const unsigned char> font_bytes() { return {ui_sans_regular_data(), ui_sans_regular_size()}; }
bool valid_text(std::string_view text) noexcept { return utf8::valid(text); }
} // namespace
DashboardView::DashboardView(std::uint64_t idle)
    : text_(font_bytes(), repertoire, text_styles, ui::TextFont::MissingGlyph::QuestionMark),
      headings_({ui_sans_bold_data(), ui_sans_bold_size()}, repertoire, std::array{theme.hostname},
                ui::TextFont::MissingGlyph::QuestionMark),
      failure_text_({ui_sans_bold_data(), ui_sans_bold_size()}, "AwariaOffline", std::array{failure_style}),
      numbers_({ui_sans_bold_data(), ui_sans_bold_size()}, number_styles),
      painter_(theme.metric_style(), text_, numbers_), clock_painter_(theme, text_, headings_, numbers_),
      ribbon_(idle, ui::RibbonController::Paging::Adjacent) {
    services_.reserve(13);
    time_.assign("00:00");
    previous_time_ = time_;
    date_.assign("--");
    rebuild(0);
}
void DashboardView::rebuild(std::uint64_t now) noexcept {
    std::array<unsigned, 14> spans{};
    std::fill_n(spans.begin(), services_.size() + 1, 3u);
    ui::RibbonLayout layout;
    if (layout.configure({0, 0, 240, 240}, std::span(spans).first(services_.size() + 1), 0))
        ribbon_.configure(layout, now);
}
void DashboardView::update_system(const SystemSnapshot &next) noexcept {
    SystemSnapshot sanitized = next;
    for (auto *value : {&sanitized.cpu_percent, &sanitized.ram_percent, &sanitized.storage_percent})
        if (*value && **value > 100)
            value->reset();
    if (!valid_text(sanitized.hostname.view()))
        sanitized.hostname.assign("?");
    if (!valid_text(sanitized.ipv4.view()))
        sanitized.ipv4.assign("");
    dirty_ |= painted_position_ < 240 && (sanitized.hostname.view() != snapshot_.hostname.view() ||
                                          sanitized.ipv4.view() != snapshot_.ipv4.view());
    const std::array before = {snapshot_.cpu_percent, snapshot_.ram_percent, snapshot_.storage_percent};
    const std::array after = {sanitized.cpu_percent, sanitized.ram_percent, sanitized.storage_percent};
    dirty_ |= before != after || sanitized.storage_emmc != snapshot_.storage_emmc;
    snapshot_ = sanitized;
}
bool DashboardView::update_clock(ClockTime time, std::uint64_t now) noexcept {
    if (time.hour > 23 || time.minute > 59)
        return false;
    char value[6];
    std::snprintf(value, sizeof(value), "%02u:%02u", time.hour, time.minute);
    if (!clock_initialized_) {
        time_.assign(value);
        previous_time_ = time_;
        clock_initialized_ = true;
        dirty_ = true;
        return true;
    }
    if (time_.view() == value)
        return true;
    previous_time_ = time_;
    time_.assign(value);
    clock_started_ = now;
    clock_animating_ = true;
    dirty_ |= ribbon_.offset(now) < 240;
    return true;
}
bool DashboardView::update_date(unsigned year, unsigned month, unsigned day, unsigned weekday) noexcept {
    const auto next = format_calendar_date(year, month, day, weekday);
    if (!next)
        return false;
    dirty_ |= painted_position_ < 240 && next->view() != date_.view();
    date_ = *next;
    return true;
}

bool DashboardView::replace_services(std::span<const ServiceCard> cards, std::uint64_t now) {
    if (cards.size() > 13)
        return false;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j)
            if (cards[i].owner() == cards[j].owner() && cards[i].id() == cards[j].id())
                return false;
    }
    std::string owner, id;
    auto selected = ribbon_.selected();
    if (selected && selected <= services_.size()) {
        owner = services_[selected - 1].owner();
        id = services_[selected - 1].id();
    }
    bool structural = cards.size() != services_.size();
    bool changed = false;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i >= services_.size() || services_[i].owner() != cards[i].owner() ||
            services_[i].id() != cards[i].id())
            structural = true;
        if (i >= services_.size() || !(services_[i] == cards[i]))
            changed |=
                int((i + 1) * 240) - painted_position_ < 240 && int((i + 2) * 240) - painted_position_ > 0;
    }
    const auto health_before = service_health();
    services_.assign(cards.begin(), cards.end());
    rebuild(now);
    if (structural) {
        auto next = std::min(selected, services_.size());
        for (std::size_t i = 0; i < services_.size(); ++i)
            if (services_[i].owner() == owner && services_[i].id() == id)
                next = i + 1;
        ribbon_.select(next, now, false);
    }
    dirty_ |= changed || structural || (painted_position_ < 240 && health_before != service_health());
    return true;
}
DashboardView::ServiceHealth DashboardView::service_health() const noexcept {
    if (!services_.size())
        return ServiceHealth::Empty;
    auto result = ServiceHealth::Ok;
    for (std::size_t i = 0; i < services_.size(); ++i) {
        if (services_[i].failed())
            return ServiceHealth::Error;
        if (services_[i].state() == dash5::TileState::Warning)
            result = ServiceHealth::Warning;
    }
    return result;
}
bool DashboardView::has_attention() const noexcept {
    for (std::size_t i = 0; i < services_.size(); ++i)
        if (services_[i].attention() || services_[i].offline() ||
            services_[i].state() != dash5::TileState::Ok)
            return true;
    return false;
}
void DashboardView::pointer(TouchSample sample, std::uint64_t now) noexcept {
    using E = ui::RibbonController::Event;
    const auto event = sample.kind == TouchSample::Kind::Down   ? E::Down
                       : sample.kind == TouchSample::Kind::Move ? E::Move
                       : sample.kind == TouchSample::Kind::Up   ? E::Up
                                                                : E::Cancel;
    auto before = ribbon_.offset(now);
    ribbon_.pointer(event, sample.x, sample.y, now);
    dirty_ |= before != ribbon_.offset(now);
}
void DashboardView::update(std::uint64_t now) noexcept { ribbon_.update(now); }
bool DashboardView::clock_active(std::uint64_t now) const noexcept {
    return clock_animating_ && (now < clock_started_ || now - clock_started_ < 240);
}
bool DashboardView::needs_redraw(std::uint64_t now) const noexcept {
    const auto *visible = visible_service(now);
    const bool scrolling =
        visible && visible->frame() == marquee_frame_ &&
        marquee_frame_->marquee_changed(rendered_at_ - marquee_started_,
                                        now >= marquee_started_ ? now - marquee_started_ : 0);
    return scrolling || dirty_ || last_animation_ || (ribbon_.offset(now) < 240 && clock_active(now)) ||
           ribbon_.animating(now) || static_cast<int>(std::lround(ribbon_.offset(now))) != painted_position_;
}
const ServiceCard *DashboardView::visible_service(std::uint64_t now) const noexcept {
    if (ribbon_.animating(now))
        return nullptr;
    const int position = static_cast<int>(std::lround(ribbon_.offset(now)));
    if (position <= 0 || position % 240 || static_cast<std::size_t>(position / 240) > services_.size())
        return nullptr;
    const auto &card = services_[position / 240 - 1];
    return card.has_content() ? &card : nullptr;
}
std::optional<std::uint64_t> DashboardView::next_visual_change(std::uint64_t now) const noexcept {
    const auto *visible = visible_service(now);
    if (!visible || visible->frame() != marquee_frame_)
        return {};
    const auto elapsed = now >= marquee_started_ ? now - marquee_started_ : 0;
    const auto delta = marquee_frame_->next_marquee_change(elapsed);
    if (!delta)
        return {};
    return now > UINT64_MAX - *delta ? UINT64_MAX : now + *delta;
}
void DashboardView::draw_service(ui::Surface &surface, int x, const ServiceCard &c, std::uint64_t elapsed) {
    if (c.has_content()) {
        c.frame()->paint(surface, x, elapsed);
    } else {
        ui::TilePainter::panel(surface, {x + 4, 4, 232, 232}, theme.error.panel);
        using Layout = kafel::LargeTileLayout;
        const int left = x + Layout::inset;
        const int width = 240 - 2 * Layout::inset;
        const auto title_ink = headings_.ink_bounds(c.title(), theme.hostname);
        headings_.draw(surface, {left, Layout::title_top, width, 30}, left, Layout::title_top - title_ink.y,
                       c.title(), theme.hostname);
        // The fallback uses the same baselines as the service composition.
        // Only these two fixed labels need precomputed 60px glyphs.
        failure_text_.draw(surface, {left, 116, width, 80}, left,
                           Layout::value_baseline - failure_text_.cap_height(failure_style),
                           c.offline() ? "Offline" : "Awaria", failure_style);
        text_.draw(surface, {left, 198, width, 34}, left,
                   Layout::caption_baseline - text_.cap_height(theme.date),
                   c.offline() ? "Brak komunikacji" : c.message(), theme.date);
    }
    if (!c.offline() && c.state() == dash5::TileState::Warning) {
        surface.rectangle({x + 160, 8, 72, 24}, theme.warning.panel.fill);
        text_.draw(surface, {x + 160, 8, 72, 24}, x + 164, 13, "UWAGA", theme.service_state);
    }
}
bool DashboardView::render(Rgb565Surface &output, std::uint64_t now) {
    if (output.width() != 240 || output.height() != 320)
        return false;
    update(now);
    ui::Surface surface(240, 320, rgb_);
    surface.clear(theme.background);
    surface.set_clip({0, 0, 240, 240});
    auto position = static_cast<int>(std::lround(ribbon_.offset(now)));
    const auto *visible = visible_service(now);
    if (!visible)
        marquee_frame_.reset();
    else if (visible->frame() != marquee_frame_ ||
             marquee_page_ != static_cast<std::size_t>(position / 240)) {
        marquee_frame_ = visible->frame();
        marquee_page_ = static_cast<std::size_t>(position / 240);
        marquee_started_ = now;
    }
    for (std::size_t page = 0; page < services_.size() + 1; ++page) {
        int x = static_cast<int>(page) * 240 - position;
        if (x >= 240 || x + 240 <= 0)
            continue;
        if (!page) {
            float progress = clock_active(now)
                                 ? static_cast<float>(now >= clock_started_ ? now - clock_started_ : 0) / 240
                                 : 1;
            const auto health = service_health();
            const auto status = health == ServiceHealth::Empty     ? "BRAK"
                                : health == ServiceHealth::Error   ? "BŁĄD"
                                : health == ServiceHealth::Warning ? "UWAGA"
                                                                   : "OK";
            const auto color = health == ServiceHealth::Error     ? theme.error.accent
                               : health == ServiceHealth::Warning ? theme.warning.accent
                                                                  : theme.secondary.color;
            clock_painter_.paint(
                surface, {x + 4, 4, 232, 232},
                {.hostname = snapshot_.hostname.view().empty() ? "--" : snapshot_.hostname.view(),
                 .time = time_.view(),
                 .date = date_.view(),
                 .previous_time = previous_time_.view(),
                 .clock_progress = progress,
                 .subheading = snapshot_.ipv4.view(),
                 .status = status,
                 .status_color = color});
        } else
            draw_service(surface, x, services_[page - 1],
                         visible && page == marquee_page_ && now >= marquee_started_ ? now - marquee_started_
                                                                                     : 0);
    }
    surface.set_clip({4, 240, 232, 76});
    ui::RibbonLayout lower;
    constexpr std::array spans = {1u, 1u, 1u};
    lower.configure({4, 240, 232, 76}, spans);
    const std::array values = {snapshot_.cpu_percent, snapshot_.ram_percent, snapshot_.storage_percent};
    for (std::size_t i = 0; i < 3; ++i) {
        auto bounds = lower.tile(i);
        bounds.x += 4;
        bounds.y += 240;
        painter_.metric(surface, bounds,
                        {i == 0                   ? "CPU"
                         : i == 1                 ? "RAM"
                         : snapshot_.storage_emmc ? "eMMC"
                                                  : "DISK",
                         values[i]});
    }
    if (!output.copy_rgb888(rgb_))
        return false;
    dirty_ = false;
    last_animation_ = (ribbon_.offset(now) < 240 && clock_active(now)) || ribbon_.animating(now);
    painted_position_ = position;
    rendered_at_ = now;
    return true;
}
} // namespace kafel
