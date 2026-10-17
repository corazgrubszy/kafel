#include "dashboard/dashboard_view.hpp"
#include "text/styled_text_reader.hpp"
#include "text/text_rows.hpp"
#include "text/marquee_text.hpp"
#include "ui/caption_cycle.hpp"
#include "system/system_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
namespace kafel {
namespace {
extern "C" const unsigned char* ui_bundled_font_data();
extern "C" std::size_t ui_bundled_font_size();
bool valid_text(std::string_view text) noexcept {
    while(!text.empty()) if(!utf8::decode(text).valid) return false;
    return true;
}
constexpr auto& theme=ui::rose_pine_moon;
constexpr std::array text_styles={theme.eyebrow,theme.hostname,theme.date,theme.label,theme.unit,
    theme.service_name,theme.service_state,theme.service_detail,theme.attention};
constexpr std::array number_styles={theme.clock_number,theme.metric_number,theme.metric_number_compact};
constexpr std::string_view repertoire=
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
    "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ°–—";
std::span<const unsigned char> font_bytes() { return {ui_bundled_font_data(),ui_bundled_font_size()}; }
ui::Color color565(Pixel value) {
    return {static_cast<unsigned char>(((value>>11)&31)*255/31),static_cast<unsigned char>(((value>>5)&63)*255/63),static_cast<unsigned char>((value&31)*255/31)};
}
struct Encoded { std::array<char,4> bytes{}; std::size_t size=0; std::string_view view() const { return {bytes.data(),size}; } };
Encoded encode(char32_t cp) {
    Encoded out;
    if(cp<0x80) { out.bytes[0]=static_cast<char>(cp); out.size=1; }
    else if(cp<0x800) { out.bytes[0]=static_cast<char>(0xc0|(cp>>6)); out.bytes[1]=static_cast<char>(0x80|(cp&63)); out.size=2; }
    else if(cp<0x10000) { out.bytes[0]=static_cast<char>(0xe0|(cp>>12)); out.bytes[1]=static_cast<char>(0x80|((cp>>6)&63)); out.bytes[2]=static_cast<char>(0x80|(cp&63)); out.size=3; }
    else { out.bytes[0]=static_cast<char>(0xf0|(cp>>18)); out.bytes[1]=static_cast<char>(0x80|((cp>>12)&63)); out.bytes[2]=static_cast<char>(0x80|((cp>>6)&63)); out.bytes[3]=static_cast<char>(0x80|(cp&63)); out.size=4; }
    return out;
}
void bin(ui::Surface& surface,int x,int y,ui::Color color) {
    // Approved U+1F5D1 glyph: lid with handle, one-pixel gap, solid body.
    surface.rectangle({x+4,y,2,2},color);
    surface.rectangle({x,y+2,10,2},color);
    surface.rectangle({x,y+5,10,12},color);
}
}
DashboardView::DashboardView(std::uint64_t idle_timeout_ms):
    text_(font_bytes(),repertoire,text_styles,ui::TextFont::MissingGlyph::QuestionMark),
    numbers_(number_styles),painter_(theme,text_,numbers_),ribbon_(idle_timeout_ms) {
    time_.assign("00:00"); previous_time_=time_; date_.assign("--"); rebuild_ribbon(0);
}
void DashboardView::rebuild_ribbon(std::uint64_t now) noexcept {
    std::array<unsigned,3+service_tile_capacity> spans{};
    std::fill_n(spans.begin(),3,1U);
    for(std::size_t i=0;i<count_;++i) spans[i+3]=services_[i].span;
    ui::RibbonLayout layout;
    if(layout.configure(viewport,std::span<const unsigned>(spans.data(),3+count_))) {
        ribbon_.configure(layout,now);
        for(std::size_t i=0;i<count_;++i) services_[i].width=layout.tile(i+3).width;
    }
}
void DashboardView::update_system(const SystemSnapshot& next) noexcept {
    SystemSnapshot sanitized=next;
    for(auto* value:{&sanitized.cpu_percent,&sanitized.ram_percent,&sanitized.storage_percent})
        if(*value && **value>100) value->reset();
    if(!valid_text(sanitized.hostname.view())) sanitized.hostname.assign("?");
    if(!valid_text(sanitized.ipv4.view())) sanitized.ipv4.assign("");
    dirty_|=sanitized.hostname.view()!=snapshot_.hostname.view()||sanitized.ipv4.view()!=snapshot_.ipv4.view();
    const std::array before={snapshot_.cpu_percent,snapshot_.ram_percent,snapshot_.storage_percent};
    const std::array after={sanitized.cpu_percent,sanitized.ram_percent,sanitized.storage_percent};
    for(std::size_t i=0;i<3;++i) {
        const auto tile=ribbon_.layout().tile(i);
        if(tile.x-painted_position_<viewport.width&&tile.right()-painted_position_>0)
            dirty_|=before[i]!=after[i]||(i==2&&sanitized.storage_emmc!=snapshot_.storage_emmc);
    }
    snapshot_=sanitized;
}
bool DashboardView::update_clock(ClockTime time,std::uint64_t now) noexcept {
    if(time.hour>23||time.minute>59) return false;
    char value[6]; std::snprintf(value,sizeof(value),"%02u:%02u",time.hour,time.minute);
    if(!clock_initialized_) { time_.assign(value); previous_time_=time_; clock_initialized_=true; dirty_=true; return true; }
    if(time_.view()==value) return true;
    previous_time_=time_; time_.assign(value); clock_started_=now; clock_animating_=true; dirty_=true; return true;
}
bool DashboardView::update_date(unsigned year,unsigned month,unsigned day,unsigned weekday) noexcept {
    const auto next=format_calendar_date(year,month,day,weekday);
    if(!next) return false;
    dirty_|=next->view()!=date_.view(); date_=*next; return true;
}
void DashboardView::assign_service(Service& service,const ServiceTileStatus& tile,std::uint64_t now) {
    const auto& status=tile.content;
    const auto previous_count=service.metric?service.metric->auxiliary_count():0;
    const auto previous_period=caption_period(service);
    service.metric.reset();
    if(tile.metric) {
        service.metric.emplace();service.metric->assign(*tile.metric);
        service.caption_widths.fill(0);
        for(std::size_t i=0;i<tile.metric->caption_count;++i)
            service.caption_widths[i]=static_cast<int>(std::ceil(text_.measure(tile.metric->captions[i],theme.label)));
    }
    if(previous_count!=(service.metric?service.metric->auxiliary_count():0)||previous_period!=caption_period(service))
        service.caption_started=now;
    service.name.assign(status.name);service.detail.assign(status.detail);service.status.assign(status.status);
    service.alarm=status.alarm;service.attention=status.attention;
    const auto rows=TextRows::split(service.detail.view());
    service.detail_width=rich_width(rows.main);
    service.detail_caption_width=rich_width(rows.caption,true);
    service.marquee_started=now;
    // Compose title and star before rounding: each run has its own glyph bearings.
    const float title_width=text_.measure(service.name.view(),theme.service_name);
    const float star_width=compact_header(service)&&service.attention?text_.measure("*",theme.attention):0;
    service.name_width=static_cast<int>(std::ceil(title_width+star_width));
    service.status_width=static_cast<int>(std::ceil(text_.measure(service.status.view(),theme.service_state)));
}
bool DashboardView::same_service(const Service& service,const ServiceTileStatus& tile) noexcept {
    const auto& status=tile.content;
    if(service.name.view()!=status.name||service.detail.view()!=status.detail||service.status.view()!=status.status||
       service.alarm!=status.alarm||service.attention!=status.attention||service.metric.has_value()!=tile.metric.has_value()) return false;
    if(service.metric) {
        const auto& a=*service.metric;const auto& b=*tile.metric;
        if(a.value.view()!=b.value||a.unit.view()!=b.unit||a.caption_count!=b.caption_count||a.level!=b.level) return false;
        for(std::size_t i=0;i<a.caption_count;++i) if(a.captions[i].view()!=b.captions[i]) return false;
    }
    return true;
}
bool DashboardView::valid_service(const ServiceStatus& status) noexcept {
    return status.name.size()<=48&&status.detail.size()<=service_detail_bytes&&status.status.size()<=15&&
        valid_text(status.name)&&valid_text(status.detail)&&TextRows::valid(status.detail)&&valid_text(status.status);
}
bool DashboardView::replace_services(std::span<const ServiceTileStatus> tiles,std::uint64_t now) noexcept {
    if(tiles.size()>services_.size()) return false;
    for(std::size_t i=0;i<tiles.size();++i) {
        const auto& tile=tiles[i];
        if(tile.span<1||tile.span>3||tile.service_id.empty()||tile.service_id.size()>32||
           tile.tile_id.empty()||tile.tile_id.size()>32||!valid_service(tile.content)) return false;
        if(tile.metric) {
            const auto& metric=*tile.metric;
            if(!valid_metric_number(metric.value)||metric.unit.size()>8||!valid_text(metric.unit)||
               metric.caption_count>metric_caption_capacity||metric.auxiliary_count()>metric_caption_capacity||(metric.level&&*metric.level>10000)) return false;
            for(std::size_t j=0;j<metric.caption_count;++j)
                if(metric.captions[j].empty()||metric.captions[j].size()>64||!valid_text(metric.captions[j])) return false;
        }
        for(std::size_t j=0;j<i;++j) if(tiles[j].service_id==tile.service_id&&tiles[j].tile_id==tile.tile_id) return false;
    }
    const auto selected=ribbon_.selected();
    FixedText<33> owner,key;
    if(selected>=3&&selected-3<count_) { owner=services_[selected-3].service_id;key=services_[selected-3].tile_id; }
    bool prefix=tiles.size()>=count_;
    for(std::size_t i=0;i<count_&&prefix;++i) prefix=services_[i].service_id.view()==tiles[i].service_id&&
        services_[i].tile_id.view()==tiles[i].tile_id&&services_[i].span==tiles[i].span;
    if(prefix&&tiles.size()==count_) {
        const bool attention_before=has_attention();
        bool visible_change=false;
        for(std::size_t i=0;i<tiles.size();++i) {
            if(!same_service(services_[i],tiles[i])) {
                visible_change|=visible(i,now);
                assign_service(services_[i],tiles[i],now);
            }
        }
        dirty_|=visible_change||attention_before!=has_attention();
        return true;
    }
    // Validate the whole snapshot before replacing any retained strings.
    count_=tiles.size();
    for(std::size_t i=0;i<count_;++i) {
        auto& service=services_[i]; const auto& tile=tiles[i];
        service.service_id.assign(tile.service_id);service.tile_id.assign(tile.tile_id);service.span=tile.span;
        assign_service(service,tile,now);
    }
    rebuild_ribbon(now);
    if(!prefix) {
        std::size_t next=std::min(selected,2+count_);
        if(!owner.view().empty()) for(std::size_t i=0;i<count_;++i)
            if(services_[i].service_id.view()==owner.view()&&services_[i].tile_id.view()==key.view()) { next=i+3;break; }
        ribbon_.select(next,now,false);
    }
    for(std::size_t i=0;i<count_;++i) services_[i].caption_started=now;
    painted_scroll_={}; dirty_=true;
    return true;
}
bool DashboardView::has_attention() const noexcept {
    for(std::size_t i=0;i<count_;++i) if(services_[i].attention||services_[i].alarm) return true;
    return false;
}
void DashboardView::pointer(TouchSample sample,std::uint64_t now) noexcept {
    using E=ui::RibbonController::Event;
    const auto event=sample.kind==TouchSample::Kind::Down?E::Down:sample.kind==TouchSample::Kind::Move?E::Move:sample.kind==TouchSample::Kind::Up?E::Up:E::Cancel;
    const auto before=ribbon_.offset(now);
    ribbon_.pointer(event,sample.x,sample.y,now);
    dirty_|=before!=ribbon_.offset(now);
    if(event==E::Down||event==E::Up||event==E::Cancel) {
        for(std::size_t i=0;i<count_;++i) services_[i].marquee_started=now;
    }
}
void DashboardView::update(std::uint64_t now) noexcept {
    ribbon_.update(now);
    if(ribbon_.animating(now)||ribbon_.dragging()) {
        for(std::size_t i=0;i<count_;++i) {
            services_[i].marquee_started=now;services_[i].caption_started=now;
        }
    }
}
bool DashboardView::clock_active(std::uint64_t now) const noexcept {
    return clock_animating_&&(now<clock_started_||now-clock_started_<240);
}
std::uint64_t DashboardView::marquee_time(const Service& service,std::uint64_t now) const noexcept {
    return ribbon_.animating(now)||ribbon_.dragging()||now<service.marquee_started?0:now-service.marquee_started;
}
bool DashboardView::visible(std::size_t index,std::uint64_t now) const noexcept {
    const auto tile=ribbon_.layout().tile(3+index); const double position=ribbon_.offset(now);
    return tile.x-position<viewport.width&&tile.right()-position>0;
}
bool DashboardView::compact_header(const Service& service) noexcept {
    return service.span==1&&!service.alarm&&service.status.view()=="OK";
}
int DashboardView::name_viewport(const Service& service) noexcept {
    if(compact_header(service))return std::max(0,service.width-16);
    // Shared by drawing and marquee scheduling: reserve the actual state width.
    return std::max(0,service.width-16-service.status_width-8-(service.attention?18:0));
}
std::uint64_t DashboardView::caption_period(const Service& service) noexcept {
    auto period=ui::CaptionCycle::period;
    if(service.metric) for(std::size_t i=0;i<service.metric->caption_count;++i) {
        const auto overflow=std::max(0,service.caption_widths[i]-(service.width-16));
        const auto travel=(static_cast<std::uint64_t>(overflow)*1000+MarqueeText::pixels_per_second-1)/MarqueeText::pixels_per_second;
        if(overflow) period=std::max(period,2*(MarqueeText::pause_ms+travel)+2*ui::CaptionCycle::fade);
    }
    return (period+ui::CaptionCycle::frame-1)/ui::CaptionCycle::frame*ui::CaptionCycle::frame;
}
std::uint64_t DashboardView::caption_time(const Service& service,std::uint64_t now) const noexcept {
    return ribbon_.animating(now)||ribbon_.dragging()||now<service.caption_started?0:now-service.caption_started;
}
DashboardView::ScrollOffsets DashboardView::scroll_offsets(std::size_t index,std::uint64_t now) const noexcept {
    if(!visible(index,now)) return {};
    const auto& service=services_[index]; const auto elapsed=marquee_time(service,now);
    const auto name=MarqueeText::offset(service.name_width,name_viewport(service),elapsed);
    if(service.metric&&service.metric->auxiliary_count()) {
        const auto caption=ui::CaptionCycle::sample(static_cast<unsigned>(service.metric->auxiliary_count()),caption_time(service,now),caption_period(service));
        return {name,MarqueeText::offset(service.caption_widths[caption.index],std::max(1,service.width-16),caption.elapsed),caption.index,caption.alpha};
    }
    return {name,service.metric?0:MarqueeText::offset(service.detail_width,std::max(1,service.width-16),elapsed),0,255,
        service.metric?0:MarqueeText::offset(service.detail_caption_width,std::max(1,service.width-16),elapsed)};
}
bool DashboardView::needs_redraw(std::uint64_t now) const noexcept {
    if(dirty_||last_animation_||clock_active(now)||ribbon_.animating(now)||
       static_cast<int>(std::lround(ribbon_.offset(now)))!=painted_position_) return true;
    for(std::size_t i=0;i<count_;++i) if(scroll_offsets(i,now)!=painted_scroll_[i]) return true;
    return false;
}
std::optional<std::uint64_t> DashboardView::next_visual_change(std::uint64_t now) const noexcept {
    std::optional<std::uint64_t> delay;
    auto consider_delay=[&](std::optional<std::uint64_t> next) {
        if(next&&(!delay||*next<*delay)) delay=next;
    };
    auto consider=[&](int width,int viewport_width,std::uint64_t elapsed) {
        consider_delay(MarqueeText::next_change(width,viewport_width,elapsed));
    };
    if(!ribbon_.animating(now)&&!ribbon_.dragging()) for(std::size_t i=0;i<count_;++i) if(visible(i,now)) {
        const auto& service=services_[i];
        consider(service.name_width,name_viewport(service),marquee_time(service,now));
        if(service.metric) {
            const auto elapsed=caption_time(service,now);
            const auto count=static_cast<unsigned>(service.metric->auxiliary_count());
            const auto caption=ui::CaptionCycle::sample(count,elapsed,caption_period(service));
            consider_delay(ui::CaptionCycle::next_change(count,elapsed,caption_period(service)));
            if(count) consider(service.caption_widths[caption.index],std::max(1,service.width-16),caption.elapsed);
        } else {
            consider(service.detail_width,std::max(1,service.width-16),marquee_time(service,now));
            consider(service.detail_caption_width,std::max(1,service.width-16),marquee_time(service,now));
        }
    }
    if(!delay) return {};
    return *delay>UINT64_MAX-now?UINT64_MAX:now+*delay;
}
int DashboardView::rich_width(std::string_view detail,bool small) const {
    const auto& text_style=small?theme.label:theme.service_detail;
    StyledTextReader reader(detail,0); float width=0;
    while(const auto glyph=reader.next()) {
        const auto encoded=encode(glyph->glyph);
        width+=glyph->glyph==U'🗑'?14:text_.measure(encoded.view(),text_style);
    }
    return static_cast<int>(std::ceil(width));
}
void DashboardView::rich_draw(ui::Surface& surface,ui::PixelRect clip,int x,int y,std::string_view detail,bool small) const {
    const auto& text_style=small?theme.label:theme.service_detail;
    StyledTextReader reader(detail,rgb(theme.service_detail.color.r,theme.service_detail.color.g,theme.service_detail.color.b));
    float pen=static_cast<float>(x);
    const auto saved=surface.clip(); surface.set_clip(clip);
    while(const auto glyph=reader.next()) {
        const auto encoded=encode(glyph->glyph); const auto color=color565(glyph->color);
        if(glyph->glyph==U'🗑') { bin(surface,static_cast<int>(std::lround(pen)),y,color); pen+=14; }
        else { auto style=text_style; style.color=color; text_.draw(surface,clip,static_cast<int>(std::lround(pen)),y,encoded.view(),style); pen+=text_.measure(encoded.view(),style); }
        if(pen>=clip.right()) break;
    }
    surface.set_clip(saved);
}
void DashboardView::draw_service(ui::Surface& surface,ui::PixelRect bounds,const Service& service,ScrollOffsets offsets) {
    const ui::StatusStyle style=service.status.view()=="OFFLINE"?theme.offline:
        service.status.view()=="UWAGA"?theme.warning:service.alarm?theme.error:
        ui::StatusStyle{theme.metric,theme.clock_number.color};
    const auto accent=style.accent;
    ui::TilePainter::panel(surface,bounds,style.panel);
    const int inset=8;
    ui::PixelRect name_clip{std::max(viewport.x,bounds.x+inset),bounds.y+7,0,20};
    name_clip.width=std::max(0,std::min(viewport.right(),bounds.x+inset+(name_viewport(service)))-name_clip.x);
    text_.draw(surface,name_clip,bounds.x+inset-offsets.name,bounds.y+10,service.name.view(),theme.service_name);
    const auto state_width=service.status_width;
    auto state_style=theme.service_state; state_style.color=accent;
    if(!compact_header(service)) {
        text_.draw(surface,bounds,bounds.right()-inset-static_cast<int>(std::ceil(state_width)),bounds.y+10,service.status.view(),state_style);
        if(service.attention)text_.draw(surface,bounds,bounds.right()-inset-state_width-18,bounds.y+10,"*",theme.attention);
    }else if(service.attention) {
        const int star_x=bounds.x+inset-offsets.name+static_cast<int>(std::lround(text_.measure(service.name.view(),theme.service_name)));
        text_.draw(surface,name_clip,star_x,bounds.y+13,"*",theme.attention);
    }
    if(service.metric) {
        painter_.value(surface,bounds,{service.metric->value.view(),service.metric->unit.view()});
        if(service.metric->level&&offsets.caption==service.metric->caption_count) {
            painter_.level(surface,bounds,*service.metric->level,offsets.alpha,style.panel.fill);
        } else if(service.metric->caption_count) {
            auto caption_style=theme.label;
            auto fade=[&](unsigned char fg,unsigned char bg) {
                return static_cast<unsigned char>((fg*offsets.alpha+bg*(255-offsets.alpha)+127)/255);
            };
            caption_style.color={fade(caption_style.color.r,style.panel.fill.r),fade(caption_style.color.g,style.panel.fill.g),fade(caption_style.color.b,style.panel.fill.b)};
            text_.draw(surface,{bounds.x+8,bounds.y+53,bounds.width-16,18},bounds.x+8-offsets.detail,bounds.y+56,
                       service.metric->captions[offsets.caption].view(),caption_style);
        }
        return;
    }
    const auto rows=TextRows::split(service.detail.view());
    if(rows.stacked) {
        const int left=std::max(viewport.x,bounds.x+inset);
        const int width=std::max(0,std::min(viewport.right(),bounds.right()-inset)-left);
        rich_draw(surface,{left,bounds.y+28,width,25},
                  bounds.x+inset-offsets.detail,bounds.y+29,rows.main);
        rich_draw(surface,{left,bounds.y+53,width,18},
                  bounds.x+inset-offsets.detail_caption,bounds.y+56,rows.caption,true);
        return;
    }
    ui::PixelRect detail_clip{std::max(viewport.x,bounds.x+inset),bounds.y+30,0,32};
    detail_clip.width=std::max(0,std::min(viewport.right(),bounds.right()-inset)-detail_clip.x);
    rich_draw(surface,detail_clip,bounds.x+inset-offsets.detail,bounds.y+37,service.detail.view());
    surface.rectangle({bounds.x+inset,bounds.bottom()-12,24,2},accent);
}
bool DashboardView::render(Surface& output,std::uint64_t now) {
    if(output.width()!=240||output.height()!=320) return false;
    update(now);
    ui::Surface surface(240,320,rgb_); surface.clear(theme.background);
    const float progress=clock_active(now)?static_cast<float>(now>=clock_started_?now-clock_started_:0)/240:1;
    painter_.clock(surface,{4,4,232,232},{"",snapshot_.hostname.view().empty()?"--":snapshot_.hostname.view(),
        time_.view(),date_.view(),previous_time_.view(),progress,has_attention(),snapshot_.ipv4.view()});
    const auto position=static_cast<int>(std::lround(ribbon_.offset(now)));
    surface.set_clip(viewport);
    const std::array<std::optional<unsigned>,3> values={snapshot_.cpu_percent,snapshot_.ram_percent,snapshot_.storage_percent};
    for(std::size_t i=0;i<ribbon_.layout().size();++i) {
        auto bounds=ribbon_.layout().tile(i); bounds.x+=viewport.x-position; bounds.y+=viewport.y;
        if(bounds.right()<=viewport.x||bounds.x>=viewport.right()) continue;
        if(i<3) painter_.metric(surface,bounds,{i==0?"CPU":i==1?"RAM":snapshot_.storage_emmc?"eMMC":"DISK",values[i]});
        else draw_service(surface,bounds,services_[i-3],scroll_offsets(i-3,now));
    }
    if(!output.copy_rgb888(rgb_)) return false;
    dirty_=false; last_animation_=clock_active(now)||ribbon_.animating(now); painted_position_=position;
    for(std::size_t i=0;i<count_;++i) painted_scroll_[i]=scroll_offsets(i,now);
    return true;
}
}
