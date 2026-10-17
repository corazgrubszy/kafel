#include "dashboard/dashboard_view.hpp"
#include <cstdio>
void check(bool,const char*);
void test_waste_tile() {
    using namespace kafel;
    const auto packet=parse_service_message("DASH/4\tkafel-waste\tKafel Waste\nTILE\twaste\t1\tok\tattention\tŚmieci\ttext\t[#B8C0C8]🗑[/][#008F39]🗑[/][#0072CE]🗑[/][#E6B800]🗑[/]\u2028Dzisiaj\n");
    check(packet.has_value(),"four-bin narrow tile parses");
    ServiceRegistry registry;registry.receive(*packet,0);
    DashboardView view;view.update_clock({16,35},0);
    check(view.replace_services(registry.tiles().view(),0),"one-column waste accepted without special renderer logic");
    using K=TouchSample::Kind;
    view.pointer({K::Down,210,270},100);view.pointer({K::Move,10,270},700);view.pointer({K::Up,10,270},710);
    std::array<Pixel,240*320> pixels{};Surface surface(pixels,240,320,240);
    view.render(surface,1400);
    const auto tile=view.ribbon().layout().tile(3);
    const int left=4+tile.x-static_cast<int>(view.ribbon().offset(1400));
    check(tile.width==74,"single-column geometry preserved");
    const std::array colors={rgb(184,192,200),rgb(0,143,57),rgb(0,114,206),rgb(230,184,0)};
    for(unsigned i=0;i<4;++i)for(int x=0;x<10;++x)
        check(pixels[277*240+left+8+static_cast<int>(i)*14+x]==colors[i],"all four complete bin bodies fit without reducing glyphs");
    bool header_star=false;
    for(int y=247;y<266;++y)for(int x=left+8;x<left+tile.width-8;++x)
    {
        const auto pixel=pixels[y*240+x];
        const unsigned red=((pixel>>11)&31)*255/31,green=((pixel>>5)&63)*255/63,blue=(pixel&31)*255/31;
        // The 12px asterisk is antialiased; its strokes need not contain a fully opaque pixel.
        header_star|=red>100&&red>green&&green>blue;
    }
    check(header_star,"attention star remains visible beside the full narrow title");
    const auto before=pixels;view.render(surface,3400);
    for(int y=247;y<266;++y)for(int x=left+8;x<left+tile.width-8;++x)
        check(before[y*240+x]==pixels[y*240+x],"full Śmieci title and attention star stay stationary in one column");
    for(int y=268;y<293;++y)for(int x=left;x<left+tile.width;++x)
        check(before[y*240+x]==pixels[y*240+x],"fitting bin row remains stationary despite header/caption scrolling");
    auto next=*packet;next.tiles[0].detail.assign("[#0072CE]🗑[/]\u202819 października");
    registry.receive(next,4000);view.replace_services(registry.tiles().view(),4000);
    view.render(surface,4000);const auto date_start=pixels;view.render(surface,6000);
    bool changed=false;
    for(int y=293;y<313;++y)for(int x=left+8;x<left+tile.width-8;++x)changed|=date_start[y*240+x]!=pixels[y*240+x];
    check(changed,"full next-pickup date scrolls in normal auxiliary font");
    std::puts("PASS one-column waste: four full-size bins, independent rows and full-date marquee");
}
