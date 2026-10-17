#include "dashboard/dashboard_view.hpp"
#include "system/system_metrics.hpp"
#include "input/event_timeline.hpp"
#include <array>
#include <cstdio>
#include <cmath>
void check(bool,const char*);
void test_dashboard() {
    using namespace kafel;
    ui::RibbonLayout geometry;
    const std::array<unsigned,5> spans={1,1,1,3,2};
    check(geometry.configure({4,240,232,76},spans),"ribbon spans configured");
    check(geometry.tile(0)==ui::PixelRect{0,0,74,76}&&geometry.tile(1).x==78&&geometry.tile(2).x==157,"ribbon matches approved grid");
    check(geometry.tile(3)==ui::PixelRect{236,0,232,76}&&geometry.tile(4).x==472,"wide service follows system metrics");
    check(geometry.maximum_offset()==393,"mixed spans content extent");
    const std::array<unsigned,1> bad={4};
    check(!geometry.configure({4,240,232,76},bad)&&geometry.size()==5,"bad layout rejected atomically");
    ui::RibbonController ribbon; ribbon.configure(geometry,0);
    using E=ui::RibbonController::Event;
    ribbon.pointer(E::Down,200,100,0); ribbon.pointer(E::Move,10,100,50); ribbon.pointer(E::Up,10,100,60);
    check(ribbon.offset(300)==0,"clock touches cannot scroll ribbon");
    ribbon.pointer(E::Down,200,270,400); ribbon.pointer(E::Move,195,180,450); ribbon.pointer(E::Up,195,180,460);
    check(ribbon.offset(800)==0,"vertical gesture does not steal horizontal navigation");
    ribbon.pointer(E::Down,200,270,900); ribbon.pointer(E::Move,10,270,1000);
    check(ribbon.grabbed_tile()==2,"gesture remembers the touched tile");
    ui::RibbonLayout appended;
    const std::array<unsigned,6> more={1,1,1,3,2,3};
    appended.configure({4,240,232,76},more); ribbon.configure(appended,1000);
    check(ribbon.grabbed_tile()==2&&ribbon.dragging(),"new service preserves active gesture identity");
    check(ribbon.offset(1000)==190,"ribbon follows finger");
    ribbon.pointer(E::Up,10,270,1010);
    check(ribbon.selected()==4&&ribbon.offset(1610)==472,"fast flick coasts across multiple anchors");
    const auto before=ribbon.offset(1100);
    ribbon.pointer(E::Down,100,270,1100);
    check(ribbon.offset(1100)==before&&!ribbon.animating(1100),"touch takes over active animation without jump");
    ribbon.pointer(E::Cancel,100,270,1110);
    check(ribbon.idle_deadline().has_value(),"idle deadline exists away from home");
    ribbon.update(*ribbon.idle_deadline());
    check(ribbon.selected()==0,"idle timeout returns to system metrics");
    check(!ribbon.select(99,0),"invalid ribbon selection rejected");
    ui::RibbonController slow,fast;
    slow.configure(appended,0); fast.configure(appended,0);
    for(auto* controller:{&slow,&fast}) controller->pointer(E::Down,200,270,0);
    slow.pointer(E::Move,10,270,1000); slow.pointer(E::Up,10,270,1010);
    fast.pointer(E::Move,10,270,100); fast.pointer(E::Up,10,270,110);
    check(fast.selected()>slow.selected(),"fast release travels farther than slow release");
    check(fast.offset(110)==190,"inertial launch preserves release position");
    const auto sampled=fast.offset(250);
    for(unsigned t=111;t<250;++t) { fast.update(t); (void)fast.offset(t); }
    check(fast.offset(250)==sampled,"spring does not depend on frame sampling");
    check(std::isfinite(fast.offset(700))&&fast.offset(710)==appended.anchor(fast.selected()),
          "inertial spring finishes exactly on mixed-width anchor");
    // The same physical reports must select the same anchor when read in one batch.
    ui::RibbonController timestamped,batched;
    timestamped.configure(appended,0); batched.configure(appended,0);
    kafel::EventTimeline event_time;
    timestamped.pointer(E::Down,200,270,10);
    timestamped.pointer(E::Move,10,270,110);
    timestamped.pointer(E::Up,10,270,120);
    batched.pointer(E::Down,200,270,event_time.map(1010,1120,120));
    batched.pointer(E::Move,10,270,event_time.map(1110,1120,120));
    batched.pointer(E::Up,10,270,event_time.map(1120,1120,120));
    check(timestamped.selected()==batched.selected()&&timestamped.offset(200)==batched.offset(200),
          "batched evdev timestamps preserve fling target and trajectory");
    check(event_time.map(1000,1120,120)==120,"regressing input timestamps cannot rewind motion");
    check(event_time.map(1200,1120,130)==130,"future event time is bounded by pump time");
    kafel::EventTimeline startup;
    check(startup.map(1,1000,10)==0,"queued events before application startup map to zero");
    ui::RibbonController continuous,unchanged;
    continuous.configure(geometry,0); unchanged.configure(geometry,0);
    for(auto* controller:{&continuous,&unchanged}) {
        controller->pointer(E::Down,80,270,0); controller->pointer(E::Move,200,270,100);
    }
    const auto pulled=continuous.offset(100);
    continuous.configure(geometry,100); continuous.configure(appended,100);
    check(continuous.offset(100)==pulled,"identical layout and appended service preserve edge pull");
    for(auto* controller:{&continuous,&unchanged}) controller->pointer(E::Up,200,270,110);
    check(continuous.offset(150)==unchanged.offset(150),"append preserves edge return trajectory");
    const auto takeover=continuous.offset(150);
    continuous.pointer(E::Down,100,270,150); continuous.pointer(E::Move,108,270,160);
    check(continuous.offset(160)<takeover,"taking over left spring and pulling right increases left excursion");
    const double expected_raw=takeover*36/(36+takeover)-8;
    check(std::abs(continuous.offset(160)-expected_raw/(1+std::abs(expected_raw)/36))<1e-9,
          "edge takeover applies resistance once");
    ui::RibbonController moving,reference;
    moving.configure(geometry,0); reference.configure(geometry,0);
    for(auto* controller:{&moving,&reference}) {
        controller->pointer(E::Down,200,270,0); controller->pointer(E::Move,10,270,100);
        controller->pointer(E::Up,10,270,110);
    }
    moving.configure(appended,180);
    for(unsigned t=180;t<=710;t+=10)
        check(moving.offset(t)==reference.offset(t),"appending during coast preserves velocity and phase");
    ui::RibbonController right; right.configure(geometry,0); right.select(4,0,false);
    right.pointer(E::Down,200,270,10); right.pointer(E::Move,80,270,110); right.pointer(E::Up,80,270,120);
    const auto right_before=right.offset(160);
    right.pointer(E::Down,100,270,160); right.pointer(E::Move,92,270,170);
    check(right.offset(170)>right_before,"right edge takeover preserves direction too");
    ui::RibbonController edge; edge.configure(geometry,0);
    edge.pointer(E::Down,100,270,0); edge.pointer(E::Move,200,270,100);
    const auto stretched=edge.offset(100);
    check(stretched<0&&stretched>-36,"edge resistance bounds rubber-band travel");
    edge.pointer(E::Up,200,270,110);
    check(edge.offset(110)==stretched&&edge.animating(110),"edge release is continuous");
    check(std::abs(edge.offset(150))<std::abs(stretched),"edge spring returns toward content");
    check(edge.offset(710)==0&&!edge.animating(710),"spring ends exactly and sleeps");
    edge.select(4,800,false); edge.pointer(E::Down,200,270,810);
    edge.pointer(E::Move,10,270,910);
    check(edge.offset(910)>geometry.maximum_offset()&&edge.offset(910)<geometry.maximum_offset()+36,
          "right edge has the same bounded resistance");
    edge.pointer(E::Cancel,10,270,920);
    check(edge.offset(1520)==geometry.maximum_offset(),"cancel returns to legal anchor");
    ui::RibbonLayout single; const std::array<unsigned,1> one={3}; single.configure({4,240,232,76},one);
    edge.configure(single,1600); edge.select(0,1600,false);
    edge.pointer(E::Down,100,270,1610); edge.pointer(E::Move,110,270,2610);
    edge.pointer(E::Up,110,270,3000);
    check(edge.offset(3600)==0,"single tile and stale release settle safely");
    check(storage_usage(100,75)==25&&!storage_usage(0,0)&&!storage_usage(10,11),"storage percent uses total and free blocks");
    DashboardView view;
    SystemSnapshot system; system.hostname.assign("cm4stack"); system.cpu_percent=37; system.ram_percent=42; system.storage_percent=18; system.storage_emmc=true;
    view.update_system(system); view.update_clock({16,35},0); view.update_date(2026,10,16,5);
    std::array<Pixel,240*320> pixels{}; Surface output(pixels,240,320,240);
    check(view.render(output,0)&&!view.needs_redraw(100),"static new dashboard sleeps");
    check(pixels[120*240+120]==rgb(57,53,82),"approved clock colour survives RGB565 conversion");
    const auto without_ip=pixels;
    system.ipv4.assign("192.0.2.220"); view.update_system(system);
    check(view.needs_redraw(101),"new IP invalidates primary tile");
    view.render(output,101); int changed=0;
    for(int y=0;y<320;++y) for(int x=0;x<240;++x) if(pixels[y*240+x]!=without_ip[y*240+x]) {
        ++changed;
        check(x>=15&&x<205&&y>=32&&y<52,"IP changes only second header line");
    }
    check(changed>0,"bare IP is visible below hostname");
    system.ipv4.assign("255.255.255.255"); view.update_system(system);
    check(view.needs_redraw(102),"changed address redraws"); view.render(output,102);
    system.ipv4.assign(""); view.update_system(system);
    check(view.needs_redraw(103),"address loss redraws to erase old text"); view.render(output,103);
    check(pixels==without_ip,"missing IP leaves completely empty row, with no label or placeholder");
    view.update_system(system); check(!view.needs_redraw(104),"unchanged empty address stays idle");
    std::array<ServiceTileStatus,1> services={{{"infohub","waste",3,{"InfoHub","[#60A8E8]🗑[/] Dzisiaj   [#F6C177]🗑[/] Jutro","OK",false,true}}}};
    check(view.replace_services(services,100),"colored service with attention accepted");
    check(view.has_attention()&&view.needs_redraw(100),"attention invalidates main clock");
    const std::array<ServiceTileStatus,1> invalid={{{"bad","status",3,{"bad","\xc0\xaf","OK"}}}};
    check(!view.replace_services(invalid,100),"invalid service UTF-8 rejected without changing view");
    view.render(output,100);
    check(!view.needs_redraw(200),"offscreen service does not start marquee");
    view.pointer({TouchSample::Kind::Down,210,270},300); view.pointer({TouchSample::Kind::Move,10,270},400); view.pointer({TouchSample::Kind::Up,10,270},410);
    view.render(output,1200); view.render(output,1201);
    check(view.selected_tile()==3,"physical samples select service tile");
    const auto visible=pixels;
    system.cpu_percent=50; view.update_system(system);
    check(!view.needs_redraw(1202),"offscreen CPU update causes no framebuffer write");
    services[0].content={"InfoHub","Brak komunikacji z usługą","OFFLINE",true,true};
    check(view.replace_services(services,1300),"offline state accepted");
    view.render(output,1300);
    check(pixels!=visible,"offline service changes visible content and colour");
    view.render(output,1301);
    check(!view.needs_redraw(1400),"short error text does not animate indefinitely");
    view.update_clock({16,36},1500);
    check(view.needs_redraw(1500),"clock change starts digit motion");
    view.render(output,1600); const auto middle=pixels; view.render(output,1800); view.render(output,1801);
    check(middle!=pixels&&!view.needs_redraw(1802),"clock animation finishes and sleeps");
    check(!view.update_clock({24,0},0)&&!view.update_date(2026,13,1,0),"bad calendar input rejected");
    std::array<Pixel,6> padded{}; padded.fill(0xbeef);
    Surface small(padded,2,2,3);
    const std::array<std::uint8_t,12> rgb8={255,0,0,0,255,0,0,0,255,255,255,255};
    check(small.copy_rgb888(rgb8)&&padded[0]==rgb(255,0,0)&&padded[1]==rgb(0,255,0)&&
          padded[3]==rgb(0,0,255)&&padded[4]==rgb(255,255,255)&&padded[2]==0xbeef&&padded[5]==0xbeef,"RGB conversion preserves channels and padding");
    check(!small.copy_rgb888(std::span<const std::uint8_t>(rgb8).first(3)),"short RGB input rejected");
    // A heartbeat with identical content must not invalidate the scene.
    view.replace_services(services,1900);
    check(!view.needs_redraw(1900),"unchanged service heartbeat causes no redraw");
    services[0].content={"InfoHub","Bardzo długi opis usługi, którego nie da się zmieścić w jednym kaflu","OK"};
    view.replace_services(services,2000);
    view.render(output,2000); view.render(output,2001);
    check(!view.needs_redraw(2100)&&view.next_visual_change(2100).has_value(),"marquee pause sleeps with scheduled wakeup");
    std::puts("PASS: Kafel dashboard, mixed-width ribbon, physical gestures, live data, attention/offline, clock and idle scheduling");
}
