#include "dashboard/dashboard_view.hpp"
#include "ui/segmented_meter.hpp"
#include <cstdio>
#include <limits>
void check(bool,const char*);
void test_meter_tiles() {
    using namespace kafel;
    const std::string head="DASH/4\tprobe\tProbe\nTILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\t";
    const auto packet=parse_service_message(head+"Sucho\tWiatr: 5 m/s\t1529\n");
    check(packet&&packet->tiles[0].metric->auxiliary_count()==3&&packet->tiles[0].metric->level==1529,"meter is explicit final auxiliary page");
    for(const auto bad:{"", "-1", "+1", "1.5", "nan", "10001", "999999999999", " 5", "5x"})
        check(!parse_service_message(head+"Sucho\t"+bad+"\n"),"bad normalized level rejected");
    check(!parse_service_message(head+"A\tB\tC\t1529\n"),"auxiliary pages stay bounded to three");
    check(parse_service_message(head+"0\n")->tiles[0].metric->auxiliary_count()==1,"meter-only auxiliary region allowed");
    check(parse_service_message(head+"10000\n")->tiles[0].metric->level==10000,"full-scale accepted");
    ServiceRegistry registry;registry.receive(*packet,0);
    check(!registry.receive(*packet,100)->changed,"identical level stays quiet");
    auto changed=*packet;changed.tiles[0].metric->level=10000;
    check(registry.receive(changed,200)->changed,"level-only changes detected");
    auto invalid=changed;invalid.tiles[0].metric->level=10001;
    check(!registry.receive(invalid,300),"invalid constructed level rejected atomically");
    invalid=changed;invalid.tiles[0].metric->caption_count=std::numeric_limits<std::size_t>::max();
    check(!registry.receive(invalid,300),"invalid count cannot wrap through level addition");
    registry.expire(0,15200);
    check(!registry.tiles().items[0].metric,"offline removes the meter with the number");

    // Actual pixels, not just arithmetic: 23 equal bars, 22 equal gaps and end padding.
    // Geometry is independent of fonts and framebuffer.
    for(int width:{58,137,138,216}) {
        const auto plan=ui::SegmentedMeter::fit(0,width);
        check(plan.count==(width+2)/6,"segment count follows fixed pixel dimensions");
        const auto first=plan.segment(0,0,10),last=plan.segment(plan.count-1,0,10);
        check(first.x==width-last.right()||first.x+1==width-last.right(),"remainder stays at ends");
        for(int i=0;i<plan.count;++i) {
            const auto segment=plan.segment(i,0,10);
            check(segment.width==4,"all segment widths are exactly four pixels");
            if(i)check(segment.x-plan.segment(i-1,0,10).right()==2,"all internal gaps are exactly two pixels");
        }
        check(plan.filled(0)==0&&plan.filled(10000)==plan.count&&plan.filled(20000)==plan.count,"meter endpoints saturate");
    }
    const auto plan=ui::SegmentedMeter::fit(0,138);
    check(plan.count==23&&plan.left==1&&plan.segment(22,0,10).right()==137&&plan.filled(1529)==4,"approved two-column mockup dimensions and wind quantization");

    registry.receive(*packet,16000);
    DashboardView view;view.update_clock({16,35},0);view.replace_services(registry.tiles().view(),0);
    using K=TouchSample::Kind;
    view.pointer({K::Down,210,270},100);view.pointer({K::Move,10,270},700);view.pointer({K::Up,10,270},710);
    std::array<Pixel,240*320> pixels{};Surface output(pixels,240,320,240);
    view.render(output,1320);view.render(output,1321);
    check(!view.needs_redraw(1400),"text plateau sleeps");
    const auto baseline=pixels;
    bool saw_meter=false;
    std::uint64_t meter_time=0;
    for(std::uint64_t now=1400;now<18000;now+=40) {
        view.render(output,now);
        for(int y=240;y<292;++y)for(int x=4;x<236;++x)
            check(pixels[y*240+x]==baseline[y*240+x],"auxiliary rotation cannot change heading or temperature");
        // Fully opaque amber distinguishes a stable meter from text and fades.
        unsigned active=0;
        for(int x=82;x<235;++x)if(pixels[300*240+x]==rgb(246,193,119))++active;
        if(active==16) { saw_meter=true;meter_time=now;break; }
    }
    check(saw_meter,"third page renders four active 4px segments at full opacity");
    const auto actual_bounds=view.ribbon().layout().tile(3);
    const int left=4+actual_bounds.x-static_cast<int>(view.ribbon().offset(meter_time));
    const auto actual_plan=ui::SegmentedMeter::fit(left+8,actual_bounds.width-16);
    for(int i=0;i<actual_plan.count;++i) {
        const auto bar=actual_plan.segment(i,300,1);
        for(int x=bar.x;x<bar.right();++x)
            check(pixels[300*240+x]==(i<4?rgb(246,193,119):rgb(57,53,82)),"rendered segments match exact four-pixel plan");
        if(i+1<actual_plan.count)for(int x=bar.right();x<bar.right()+2;++x)
            check(pixels[300*240+x]==rgb(42,39,63),"rendered gaps are exactly two background pixels");
    }
    view.render(output,meter_time+1);
    check(!view.needs_redraw(meter_time+2),"meter plateau does not stream framebuffer writes");
    const auto deadline=view.next_visual_change(meter_time+2);
    check(deadline&&*deadline>meter_time+2,"meter schedules its next fade");
    registry.receive(changed,meter_time+3);view.replace_services(registry.tiles().view(),meter_time+3);view.render(output,meter_time+3);
    check(view.next_visual_change(meter_time+3)==deadline,"level update preserves rotation phase");
    check(!view.replace_services(std::array<ServiceTileStatus,1>{{{"bad","bad",2,{"Bad","","OK"},MetricContent{"13","°C",{},0,10001}}}},meter_time+4),"view rejects invalid direct normalized level");
    view.pointer({K::Down,10,270},20000);view.pointer({K::Move,210,270},20100);view.pointer({K::Up,210,270},20110);
    view.render(output,20800);view.render(output,20801);
    check(!view.next_visual_change(21000),"hidden meter cannot wake framebuffer");
    std::puts("PASS: generic metric-meter, bounded level, uniform segments, rotation, idle and ownership");
}
