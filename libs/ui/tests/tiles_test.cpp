#include "ui/grid.hpp"
#include "ui/tile.hpp"
#include "memory.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace {
void check(bool condition,const char* message) {
    if(!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
template<class F> void rejects(F operation) {
    bool rejected=false; try { operation(); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"invalid input must be rejected");
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    ui::Grid grid({4,240,232,76},3,4);
    check(*grid.cell(0)==ui::PixelRect{4,240,74,76},"first cell");
    check(*grid.cell(1)==ui::PixelRect{82,240,75,76},"second cell");
    check(*grid.cell(2)==ui::PixelRect{161,240,75,76},"third cell");
    check(*grid.cell(0,2)==ui::PixelRect{4,240,153,76},"two columns include gap");
    check(*grid.cell(1,2)==ui::PixelRect{82,240,154,76},"rounded two-column width");
    check(*grid.cell(0,3)==ui::PixelRect{4,240,232,76},"three columns");
    check(!grid.cell(-1)&&!grid.cell(2,2)&&!grid.cell(0,0)&&!grid.cell(0,4),"invalid spans");
    for(int width=20;width<401;++width) {
        ui::Grid g({7,11,width,30},3,4);
        check(g.cell(0)->x==7&&g.cell(2)->right()==7+width,"grid covers bounds exactly");
        check(g.cell(1)->x-g.cell(0)->right()==4&&g.cell(2)->x-g.cell(1)->right()==4,"gap invariant");
    }
    rejects([]{ui::Grid invalid({0,0,10,10},3,4);});
    rejects([]{ui::Grid invalid({0,0,10,10},std::numeric_limits<int>::min(),4);});
    ui::Grid edge({std::numeric_limits<int>::max()-232,0,232,76},3,4);
    check(edge.cell(2)->right()==std::numeric_limits<int>::max(),"safe edge arithmetic");
    check(ui::TilePainter::filled_segments(0,10)==0&&ui::TilePainter::filled_segments(1,10)==1&&
          ui::TilePainter::filled_segments(42,10)==5&&ui::TilePainter::filled_segments(100,10)==10,"meter coverage");
    const auto& theme=ui::rose_pine_moon;
    check(theme.background==ui::Color::hex(0x232136)&&theme.clock_number.color==ui::Color::hex(0xc4a7e7),"palette roles");
    const std::array styles={theme.eyebrow,theme.hostname,theme.date,theme.label,theme.unit};
    const std::array number_styles={theme.clock_number,theme.metric_number,theme.metric_number_compact};
    ui::TextFont font(argv[1],"TIME / LOCALcm4stackPIĄTEK 16 PAŹ 2026CPU RAM eMMC %0123456789",styles);
    ui::NumberFont numbers(number_styles);
    ui::TilePainter painter(theme,font,numbers);
    std::array<unsigned char,240*320*3> pixels{};
    ui::Surface surface(240,320,pixels);
    // Guard renderer clipping with a destination inset inside a larger buffer.
    std::array<unsigned char,3*4*4+2> guarded{}; guarded.front()=17; guarded.back()=29;
    ui::Surface tiny(4,4,std::span<unsigned char>(guarded).subspan(1,48));
    tiny.rectangle({-100,-100,200,200},{255,0,0}); tiny.blend(-1,0,{0,0,0},255);
    check(guarded.front()==17&&guarded.back()==29,"surface clipping guard");
    surface.clear(theme.background);
    painter.clock(surface,{4,4,232,232},{"TIME / LOCAL","cm4stack","16:35","PIĄTEK 16 PAŹ 2026"});
    for(unsigned value:{0u,1u,9u,10u,99u,100u}) painter.metric(surface,*grid.cell(0),{"CPU",value});
    const auto before=ui_test::memory();
    for(int frame=0;frame<10;++frame) {
        surface.clear(theme.background);
        painter.clock(surface,{4,4,232,232},{"TIME / LOCAL","cm4stack","23:59","PIĄTEK 16 PAŹ 2026"});
        for(int column=0;column<3;++column) painter.metric(surface,*grid.cell(column),{"RAM",42});
    }
    check(ui_test::memory().allocations==before.allocations,"no drawing allocations");
    rejects([&]{painter.metric(surface,*grid.cell(0),{"CPU",101});});
    rejects([&]{painter.clock(surface,{4,4,232,232},{"TIME / LOCAL","cm4stack","24:00",""});});
    rejects([&]{font.measure("\xc0\xaf",theme.label);});
    rejects([&]{font.measure("\xf0\x9f\x8c\x9f",theme.label);});
    const auto old=pixels;
    painter.metric(surface,*grid.cell(0),{"CPU",37});
    check(pixels!=old,"model updates pixels");
    // The neighbour and all gaps must survive a single tile update.
    const auto bounds=*grid.cell(0);
    for(int y=0;y<320;++y) for(int x=0;x<240;++x) if(x<bounds.x||x>=bounds.right()||y<bounds.y||y>=bounds.bottom())
        for(int channel=0;channel<3;++channel) check(pixels[(y*240+x)*3+channel]==old[(y*240+x)*3+channel],"tile clips its output");
    std::puts("PASS: grid rounding/spans/gaps, palette, clipping, UTF-8, ranges, model updates, zero drawing allocations");
}
