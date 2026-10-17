#include "dashboard/dashboard_view.hpp"
#include "ui/caption_cycle.hpp"
#include "ui/tile.hpp"
#include "text/text_rows.hpp"
#include <array>
#include <string>
#include <cstdio>
#include <cmath>
void check(bool,const char*);
extern "C" const unsigned char* ui_bundled_font_data();
extern "C" std::size_t ui_bundled_font_size();
void test_metric_tiles() {
    using namespace kafel;
    const auto rows=TextRows::split("[#60A8E8]🗑[/]\u2028Dzisiaj");
    check(rows.stacked&&rows.main=="[#60A8E8]🗑[/]"&&rows.caption=="Dzisiaj","two rows preserve colored glyphs and caption");
    check(!TextRows::split("Plain").stacked&&TextRows::valid("Plain"),"plain text keeps original layout");
    check(!TextRows::valid("a\u2028b\u2028c"),"text has at most two rows");
    const auto stacked=parse_service_message("DASH/4\tprobe\tProbe\nTILE\twaste\t2\tok\tattention\tŚmieci\ttext\t[#60A8E8]🗑[/]\u2028Dzisiaj\n");
    check(stacked&&stacked->tiles[0].span==2,"stacked text uses unchanged DASH/4 text fields");
    check(!parse_service_message("DASH/4\tprobe\tProbe\nTILE\twaste\t2\tok\tnormal\tTitle\ttext\ta\u2028b\u2028c\n"),"third row rejects whole snapshot");
    {
        ServiceRegistry rows_registry;rows_registry.receive(*stacked,0);
        DashboardView rows_view;rows_view.replace_services(rows_registry.tiles().view(),0);
        using K=TouchSample::Kind;
        rows_view.pointer({K::Down,210,270},100);rows_view.pointer({K::Move,10,270},200);rows_view.pointer({K::Up,10,270},210);
        std::array<Pixel,240*320> frame{};Surface screen(frame,240,320,240);
        rows_view.render(screen,1400);rows_view.render(screen,1401);
        check(!rows_view.needs_redraw(1500),"fitting stacked text sleeps between source updates");
        const auto today=frame;
        auto tomorrow=*stacked;tomorrow.tiles[0].detail.assign("[#E8C850]🗑[/]\u2028Jutro");
        rows_registry.receive(tomorrow,1600);rows_view.replace_services(rows_registry.tiles().view(),1600);rows_view.render(screen,1600);
        bool main_changed=false,caption_changed=false;
        for(int y=268;y<313;++y) for(int x=84;x<236;++x) if(today[y*240+x]!=frame[y*240+x]) {
            if(y<293) main_changed=true;else caption_changed=true;
        }
        check(main_changed&&caption_changed,"icons and caption change in the same rendered frame");
        rows_view.render(screen,1601);check(!rows_view.needs_redraw(1700),"updated stacked tile returns to sleep");
    }
    {
        using K=TouchSample::Kind;
        const auto show_services=[](DashboardView& view) {
            view.pointer({K::Down,210,270},100);view.pointer({K::Move,10,270},200);view.pointer({K::Up,10,270},210);
        };
        const auto crop_equal=[](const auto& a,const auto& b,int left,int right,int top,int bottom) {
            for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) if(a[y*240+x]!=b[y*240+x]) return false;
            return true;
        };
        const std::string long_text="ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for(bool long_main:{true,false}) {
            auto message=*stacked;
            message.tiles[0].detail.assign(long_main?long_text+"\u2028Dzisiaj":"[#60A8E8]🗑[/]\u2028"+long_text);
            ServiceRegistry source;source.receive(message,0);
            DashboardView view;view.replace_services(source.tiles().view(),0);show_services(view);
            std::array<Pixel,240*320> pixels{};Surface screen(pixels,240,320,240);
            view.render(screen,1400);const auto before=pixels;
            check(view.next_visual_change(1400).has_value(),"overflow in either row schedules a wakeup");
            view.render(screen,3400);
            check(crop_equal(before,pixels,84,236,long_main?293:268,long_main?313:293),
                  "fitting row stays stationary while the other row scrolls");
            check(!crop_equal(before,pixels,84,236,long_main?268:293,long_main?293:313),
                  "overflowing row scrolls independently");
        }
        auto message=*stacked;message.tile_count=2;
        message.tiles[0].id.assign("long");message.tiles[0].title.assign("Long");message.tiles[0].attention=false;
        message.tiles[0].detail.assign(long_text);
        message.tiles[1]=stacked->tiles[0];
        ServiceRegistry source;source.receive(message,0);
        DashboardView reference,updated;
        reference.replace_services(source.tiles().view(),0);updated.replace_services(source.tiles().view(),0);
        show_services(reference);show_services(updated);
        std::array<Pixel,240*320> expected{},actual{};
        Surface a(expected,240,320,240),b(actual,240,320,240);
        reference.render(a,1400);updated.render(b,1400);
        message.tiles[1].detail.assign("[#E8C850]🗑[/]\u2028Jutro");
        source.receive(message,2500);updated.replace_services(source.tiles().view(),2500);
        reference.render(a,3000);updated.render(b,3000);
        check(crop_equal(expected,actual,4,78,240,316),"neighbor update preserves another visible tile's scrolling phase");
        check(reference.next_visual_change(3000)==updated.next_visual_change(3000),"neighbor update preserves scrolling wakeup deadline");
    }
    const std::string header="DASH/4\tprobe\tProbe\nTILE\tvalue\t2\tok\tnormal\tValue\tmetric\t";
    const auto good=parse_service_message(header+"-3.5\t°C\tPogodnie\tWiatr: 5 m/s\n");
    check(good&&good->tiles[0].metric&&good->tiles[0].metric->value.view()=="-3.5"&&good->tiles[0].metric->caption_count==2,
          "typed metric parses signed decimal, unit and captions");
    for(auto value:{"-",".5","1.","1..2","+1","NaN","1e3","123456789"})
        check(!parse_service_message(header+value+"\t°C\n"),"invalid numeric grammar rejected");
    check(parse_service_message(header+"\t°C\n").has_value(),"missing metric value is explicit");
    check(parse_service_message(header+"8\t\n").has_value(),"unit and captions are optional");
    check(!parse_service_message(header+"8\t123456789\n"),"unit byte budget enforced");
    check(!parse_service_message(header+"8\t°C\tA\tB\tC\tD\n"),"caption count bounded");
    check(!parse_service_message(header+"8\t°C\t\n"),"empty caption is not a list item");
    check(!parse_service_message(header+"8\t°C\t"+std::string(65,'x')+"\n"),"caption byte budget enforced");
    check(!parse_service_message("DASH/3\told\tOld\n"),"DASH/3 is not retained");
    ServiceRegistry registry;registry.receive(*good,0);
    auto invalid=*good;invalid.tiles[0].metric->caption_count=metric_caption_capacity+1;
    check(!registry.receive(invalid,50),"registry rejects invalid constructed caption count before commit");
    auto changed=*good;changed.tiles[0].metric->value.assign("-2");
    check(registry.receive(changed,100)->changed,"value-only update invalidates snapshot");
    check(!registry.receive(changed,200)->changed,"identical typed snapshot stays quiet");
    changed.tiles[0].metric->captions[0].assign("Deszcz");
    check(registry.receive(changed,300)->changed,"caption-only update is detected");
    registry.expire(0,15300);
    check(!registry.tiles().items[0].metric&&registry.tiles().items[0].content.status=="OFFLINE",
          "offline replaces stale number with communication error");

    using Cycle=ui::CaptionCycle;
    check(Cycle::sample(2,0).alpha==255&&Cycle::sample(2,4639).alpha==255,"first caption is immediately readable");
    check(Cycle::sample(2,4680).alpha<255&&Cycle::sample(2,4800).index==1,"caption fades before replacement");
    check(Cycle::sample(2,4960).alpha==255&&Cycle::sample(2,9600).index==0,"caption loop and fade finish exactly");
    check(Cycle::next_change(2,100)==4540&&!Cycle::next_change(1,100),"caption plateaus and single caption sleep");
    check(Cycle::sample(2,4800,10000).index==0,"long caption interval can finish scrolling");

    const auto& theme=ui::rose_pine_moon;
    const std::array styles={theme.label,theme.unit};
    const std::array numeric={theme.metric_number,theme.metric_number_compact};
    ui::TextFont text({ui_bundled_font_data(),ui_bundled_font_size()},"CPU RAM eMMC %0123456789-#°C",styles);
    ui::NumberFont numbers(numeric);ui::TilePainter painter(theme,text,numbers);
    std::array<unsigned char,153*76*3> rgb{};ui::Surface body(153,76,rgb);body.clear(theme.metric.fill);
    check(painter.value(body,{0,0,153,76},{"-3.5","°C"}),"signed decimal fits two-column value");
    check(std::abs(numbers.draw(body,{0,0,153,76},8,29,"-3.5",theme.metric_number)-8-
                   ui::NumberFont::measure("-3.5",theme.metric_number))<.001,"numeric measure agrees with draw advances");
    check(!painter.value(body,{0,0,74,76},{"12345678","°C"}),"oversized values show overflow instead of partial number");
    check(painter.value(body,{0,0,74,76},{"","°C"}),"missing value renders without a fabricated number");
    check(theme.meter.height==10,"approved higher system bars are the default");
    for(auto label:{"CPU","RAM","eMMC"}) {
        std::array<unsigned char,74*76*3> normal{},tall{};
        ui::Surface a(74,76,normal),b(74,76,tall);
        auto previous=theme;previous.meter.height=6;ui::TilePainter baseline(previous,text,numbers);
        baseline.metric(a,{0,0,74,76},{label,42});painter.metric(b,{0,0,74,76},{label,42});
        bool differs=false;
        for(int y=0;y<76;++y) for(int x=0;x<74;++x) for(int c=0;c<3;++c) {
            const int i=(y*74+x)*3+c;
            if(normal[i]!=tall[i]) { differs=true;check(y>=57&&y<61,"higher bars change only their extra rows"); }
        }
        check(differs,"10px default differs from the previous 6px only in added rows");
    }

    const auto packet=parse_service_message("DASH/4\tprobe\tProbe\nTILE\tvalue\t2\tok\tnormal\tValue\tmetric\t8\t°C\tPogodnie\tWiatr: 5 m/s\n");
    ServiceRegistry live;live.receive(*packet,0);
    DashboardView view;view.update_clock({16,35},0);view.replace_services(live.tiles().view(),0);
    using K=TouchSample::Kind;
    view.pointer({K::Down,210,270},100);view.pointer({K::Move,10,270},700);view.pointer({K::Up,10,270},710);
    std::array<Pixel,240*320> pixels{};Surface output(pixels,240,320,240);
    view.render(output,1320);view.render(output,1321);
    check(view.ribbon().offset(1400)==157&&!view.needs_redraw(1400),"static visible metric does not stream frames");
    const auto retained=pixels;
    auto reused=*packet;reused.tiles[0].metric->value.assign("99");reused.tiles[0].metric->captions[0].assign("Deszcz");
    live.receive(reused,1350);view.render(output,1350);
    check(pixels==retained,"view owns metric value and captions independently of producer buffers");
    const auto deadline=view.next_visual_change(1400);
    check(deadline&&*deadline>1400,"caption rotation has scheduled wakeup");
    auto value_update=*packet;value_update.tiles[0].metric->value.assign("9");
    live.receive(value_update,2000);view.replace_services(live.tiles().view(),2000);view.render(output,2000);
    check(view.next_visual_change(2000)==deadline,"value refresh preserves caption clock");
    value_update.tiles[0].metric->captions[1].assign("Wiatr: 6 m/s");
    live.receive(value_update,2100);view.replace_services(live.tiles().view(),2100);view.render(output,2100);
    check(view.next_visual_change(2100)==deadline,"caption text refresh preserves cycle when its geometry stays the same");
    view.render(output,*deadline+40);check(view.needs_redraw(*deadline+80),"fade advances at bounded cadence");
    view.render(output,*deadline+ui::CaptionCycle::fade+200);view.render(output,*deadline+ui::CaptionCycle::fade+201);
    check(!view.needs_redraw(*deadline+ui::CaptionCycle::fade+240),"caption transition returns to sleep");
    view.pointer({K::Down,10,270},10000);view.pointer({K::Move,210,270},10100);view.pointer({K::Up,210,270},10110);
    view.render(output,10800);view.render(output,10801);
    check(!view.next_visual_change(11000)&&!view.needs_redraw(11000),"hidden metric captions cannot wake framebuffer");
    std::puts("PASS: typed metric, decimal glyphs, caption deadlines, unchanged system layout and approved higher bars");
}
