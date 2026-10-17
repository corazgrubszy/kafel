#include "ui/tile.hpp"
#include "ui/grid.hpp"
#include "ui/segmented_meter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace ui {
void TilePainter::panel(Surface& surface,PixelRect r,PanelStyle style) {
    surface.rounded_rectangle(r,style.radius,style.fill);
}
void TilePainter::clock(Surface& surface,PixelRect r,const ClockTile& model) const {
    if(r.width<232||r.height<100) throw std::invalid_argument("clock tile too small");
    if(model.time.size()!=5||model.time[2]!=':' ||
       model.time[0]<'0'||model.time[0]>'2'||model.time[1]<'0'||model.time[1]>'9'||
       (model.time[0]=='2'&&model.time[1]>'3')||model.time[3]<'0'||model.time[3]>'5'||model.time[4]<'0'||model.time[4]>'9')
        throw std::invalid_argument("clock expects HH:MM");
    panel(surface,r,theme_.clock);
    const int inset=theme_.clock.inset;
    if(!model.label.empty()) text_.draw(surface,r,r.x+inset+1,r.y+13,model.label,theme_.eyebrow);
    // The production clock has a readable hostname and optional second line.
    // Keep a separate clipped field for callers that supply an eyebrow.
    const PixelRect hostname_clip=model.label.empty()?
        PixelRect{r.x+inset+1,r.y+10,r.width-2*inset-30,20}:
        PixelRect{r.right()-86,r.y+10,72,20};
    const int hostname_x=model.label.empty()?hostname_clip.x:
        std::max(hostname_clip.x,r.right()-inset-4-static_cast<int>(std::ceil(text_.measure(model.hostname,theme_.hostname))));
    text_.draw(surface,hostname_clip,hostname_x,r.y+13,model.hostname,theme_.hostname);
    if(!model.subheading.empty()) {
        const PixelRect secondary_clip{hostname_clip.x,r.y+28,hostname_clip.width,20};
        text_.draw(surface,secondary_clip,hostname_clip.x,r.y+31,model.subheading,theme_.hostname);
    }
    const auto style=theme_.clock_number;
    const int top=r.bottom()-78;
    if(model.previous_time.size()==5 && model.clock_progress<1) {
        float pen=static_cast<float>(r.x+inset);
        const int height=static_cast<int>(std::ceil(32*style.scale));
        const int shift=static_cast<int>(std::lround(std::clamp(model.clock_progress,0.0f,1.0f)*height));
        for(std::size_t i=0;i<5;++i) {
            const char c=model.time[i];
            const int advance=static_cast<int>(std::ceil((c==':'?13:45)*style.scale));
            const PixelRect cell{static_cast<int>(std::lround(pen)),top,advance,height};
            if(c==model.previous_time[i]) numbers_.draw(surface,cell,cell.x,top,model.time.substr(i,1),style);
            else {
                numbers_.draw(surface,cell,cell.x,top+shift,model.previous_time.substr(i,1),style);
                numbers_.draw(surface,cell,cell.x,top-height+shift,model.time.substr(i,1),style);
            }
            pen+=(c==':'?13:45)*style.scale;
        }
    } else numbers_.draw(surface,r,r.x+inset,top,model.time,style);
    if(model.attention) text_.draw(surface,r,r.right()-24,r.y+13,"*",theme_.attention);
    text_.draw(surface,r,r.x+inset+1,r.bottom()-22,model.date,theme_.date);
}
bool TilePainter::value(Surface& surface,PixelRect r,const ValueTile& model) const {
    const int x=r.x+8;
    if(model.value.empty()) { text_.draw(surface,r,x,r.y+29,"--",theme_.label);return true; }
    auto style=theme_.metric_number;
    const float unit=model.unit.empty()?0:1+text_.measure(model.unit,theme_.unit);
    if(NumberFont::measure(model.value,style)+unit>r.width-16) style=theme_.metric_number_compact;
    if(NumberFont::measure(model.value,style)+unit>r.width-16) {
        text_.draw(surface,r,x,r.y+29,"###",theme_.label);return false;
    }
    const float end=numbers_.draw(surface,r,x,r.y+29,model.value,style);
    text_.draw(surface,r,static_cast<int>(std::lround(end))+1,r.y+34,model.unit,theme_.unit);
    return true;
}
void TilePainter::level(Surface& surface,PixelRect bounds,unsigned basis_points,unsigned alpha,Color background) const {
    const auto geometry=SegmentedMeter::fit(bounds.x+8,bounds.width-16);
    const auto opacity=std::min(alpha,255U);
    auto faded=[&](Color color) {
        auto mix=[&](unsigned fg,unsigned bg) { return static_cast<unsigned char>((fg*opacity+bg*(255-opacity)+127)/255); };
        return Color{mix(color.r,background.r),mix(color.g,background.g),mix(color.b,background.b)};
    };
    const auto filled=faded(theme_.meter.filled),empty=faded(theme_.meter.empty);
    for(int i=0;i<geometry.count;++i)
        surface.rectangle(geometry.segment(i,bounds.bottom()-9-theme_.meter.height,theme_.meter.height),
                          i<geometry.filled(basis_points)?filled:empty);
}
void TilePainter::metric(Surface& surface,PixelRect r,const MetricTile& model) const {
    if(r.width<74||r.height<76||(model.percent && *model.percent>100)) throw std::invalid_argument("invalid metric tile");
    panel(surface,r,theme_.metric);
    const int inset=theme_.metric.inset;
    text_.draw(surface,r,r.x+inset,r.y+10,model.label,theme_.label);
    if(model.percent) {
        char value[4]; std::snprintf(value,sizeof(value),"%u",*model.percent);
        const float right=numbers_.draw(surface,r,r.x+inset,r.y+29,value,*model.percent==100?theme_.metric_number_compact:theme_.metric_number);
        text_.draw(surface,r,static_cast<int>(std::lround(right))+1,r.y+34,"%",theme_.unit);
    } else text_.draw(surface,r,r.x+inset,r.y+29,"--",theme_.label);
    const auto& meter=theme_.meter;
    Grid segments({r.x+inset,r.bottom()-9-meter.height,r.width-2*inset,meter.height},meter.segments,meter.gap);
    const int filled=filled_segments(model.percent.value_or(0),meter.segments);
    for(int i=0;i<meter.segments;++i) surface.rectangle(*segments.cell(i),i<filled?meter.filled:meter.empty);
}
}
