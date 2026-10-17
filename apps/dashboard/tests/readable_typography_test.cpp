#include "dashboard/dashboard_view.hpp"
#include "system/system_metrics.hpp"
#include <cmath>
#include <cstdio>
void check(bool,const char*);
extern "C" const unsigned char* ui_bundled_font_data();
extern "C" std::size_t ui_bundled_font_size();
void test_readable_typography() {
    using kafel::format_calendar_date;
    const auto& theme=ui::rose_pine_moon;
    const std::array styles={theme.hostname,theme.date,theme.label,theme.unit,theme.service_name,theme.service_state,theme.service_detail,theme.attention};
    ui::TextFont font({ui_bundled_font_data(),ui_bundled_font_size()},
        "CPU RAM eMMC %Poniedziałek Wtorek Środa Czwartek Piątek Sobota Niedziela 0123456789. sty lut mar kwi maj cze lip sie wrz paź lis gru OFFLINE AWARIA UWAGA OK",styles);
    for(auto label:{"CPU","RAM","eMMC"}) check(font.measure(label,theme.label)<=58,"readable labels fit narrowest tile");
    check(font.measure("255.255.255.255",theme.hostname)<=190,"longest IPv4 fits second header line at 14px");
    check(theme.hostname.em_px==14&&theme.hostname.em_px==theme.date.em_px,
          "hostname and IP use the readable 14px date size");
    std::array<unsigned char,190*38*3> header{};
    ui::Surface header_surface(190,38,header); header_surface.clear(theme.clock.fill);
    font.draw(header_surface,{0,0,190,20},0,3,"Poniedziałek",theme.hostname);
    font.draw(header_surface,{0,18,190,20},0,21,"255.255.255.255",theme.hostname);
    for(int y=17;y<20;++y) for(int x=0;x<190;++x)
        check(header[(y*190+x)*3]==theme.clock.fill.r,"14px header lines keep a clear vertical gap");
    check(theme.label.em_px==14&&theme.unit.em_px==14&&
          theme.service_name.em_px==14&&theme.service_state.em_px==14,
          "ribbon labels, units and service headers use readable 14px text");
    const float numbers=3*45*theme.metric_number_compact.scale;
    check(numbers+1+font.measure("%",theme.unit)<=58,"100 percent and readable unit fit together");
    check(format_calendar_date(2026,10,16,5)->view()=="Piątek 16 paź","full Polish weekday without year");
    for(unsigned month=1;month<=12;++month) for(unsigned day=0;day<7;++day) {
        const auto formatted=format_calendar_date(9999,month,28,day);
        check(formatted&&font.measure(formatted->view(),theme.date)<=210,"all dates fit at readable size");
    }
    check(!format_calendar_date(2026,2,29,0)&&format_calendar_date(2028,2,29,0).has_value(),"calendar leap validation preserved");
    std::array<unsigned char,10*10*3> corner_pixels{};
    ui::Surface rounded(10,10,corner_pixels);
    rounded.rounded_rectangle({0,0,10,10},3,{255,0,0});
    check(corner_pixels[0]==0&&corner_pixels[5*3]==255&&corner_pixels[(5*10)*3]==255,"rounded panel has empty corners and no border along edges");
    check(corner_pixels[3]>0&&corner_pixels[3]<255,"rounded corners use partial coverage");
    for(int y=0;y<10;++y) for(int x=0;x<10;++x)
        check(corner_pixels[(y*10+x)*3]==corner_pixels[(y*10+9-x)*3]&&
              corner_pixels[(y*10+x)*3]==corner_pixels[((9-y)*10+x)*3],"rounded panel is symmetric");
    std::array<unsigned char,74*76*3> pixels{};
    ui::Surface surface(74,76,pixels); surface.clear(theme.metric.fill);
    font.draw(surface,{8,10,58,18},8,10,"CPU",theme.label);
    int rows=0;
    for(int y=10;y<28;++y) {
        bool ink=false;
        for(int x=8;x<66;++x) ink|=pixels[(y*74+x)*3]!=theme.metric.fill.r;
        rows+=ink;
    }
    check(rows>=8,"metric label has at least eight visible pixel rows");
    // The actual offline state and attention star must survive a long name.
    kafel::DashboardView view;
    kafel::SystemSnapshot system; system.hostname.assign("cm4stack"); view.update_system(system);
    view.update_clock({16,35},0); view.update_date(2026,10,16,5);
    std::array<kafel::ServiceTileStatus,1> services={{{"infohub","status",3,{"A","opis","OFFLINE",true,true}}}};
    view.replace_services(services,0);
    system.ipv4.assign("255.255.255.255"); view.update_system(system);
    std::array<kafel::Pixel,240*320> header_frame{};
    kafel::Surface header_output(header_frame,240,320,240); view.render(header_output,0);
    const auto short_hostname=header_frame;
    system.hostname.assign("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
    view.update_system(system); view.render(header_output,1);
    for(int y=0;y<320;++y) for(int x=0;x<240;++x)
        if(header_frame[y*240+x]!=short_hostname[y*240+x])
            check(x>=15&&x<205&&y>=14&&y<34,
                  "long 14px hostname clips before attention star and never touches IP");
    view.pointer({kafel::TouchSample::Kind::Down,210,270},100);
    view.pointer({kafel::TouchSample::Kind::Move,10,270},200);
    view.pointer({kafel::TouchSample::Kind::Up,10,270},210);
    std::array<kafel::Pixel,240*320> frame{}; kafel::Surface output(frame,240,320,240);
    view.render(output,900); const auto reference=frame;
    const auto clock_background=kafel::rgb(theme.clock.fill.r,theme.clock.fill.g,theme.clock.fill.b);
    // Keep the enlarged separation clear, including the former clock position.
    for(int y=195;y<211;++y) for(int x=14;x<226;++x)
        check(frame[y*240+x]==clock_background,"clock leaves visible breathing room above date");
    services[0].content={"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW","opis","OFFLINE",true,true};
    view.replace_services(services,901);
    view.render(output,901);
    for(int y=247;y<266;++y) for(int x=155;x<229;++x)
        check(frame[y*240+x]==reference[y*240+x],"long service name cannot cover state or attention");
    check(view.next_visual_change(901).has_value(),"long name uses measured header space for marquee");
    std::puts("PASS: readable typography, actual glyph height, 100%, calendar widths, long service names and state separation");
}
