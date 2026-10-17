#include "dashboard/dashboard_view.hpp"
#include <array>
#include <cstdlib>
#include <new>
namespace { bool recording=false;std::size_t allocations=0; }
void* operator new(std::size_t size) {
    if(recording) ++allocations;
    if(auto* memory=std::malloc(size?size:1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete[](void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept {std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept {std::free(memory);}
void check(bool,const char*);
void test_allocations() {
    using namespace kafel;
    DashboardView view;ServiceRegistry registry;
    SystemSnapshot system;system.hostname.assign("kafel");system.cpu_percent=37;system.ram_percent=42;
    view.update_system(system);view.update_clock({16,35},0);
    std::array<Pixel,240*320> pixels{};Surface output(pixels,240,320,240);
    bool valid=true;recording=true;
    for(std::uint64_t frame=0;frame<450;++frame) {
        const auto packet=parse_service_message("DASH/4\tprobe\tProbe\nTILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t-3.5\t°C\tSucho\tWiatr: 5 m/s\t1529\nTILE\twaste\t2\tok\tattention\tŚmieci\ttext\t[#60A8E8]🗑[/]\u2028Dzisiaj\n");
        valid&=packet.has_value();
        registry.receive(*packet,frame*40);view.replace_services(registry.tiles().view(),frame*40);
        if(frame==10)view.pointer({TouchSample::Kind::Down,210,270},frame*40);
        if(frame==15)view.pointer({TouchSample::Kind::Up,10,270},frame*40);
        if(frame==80)view.update_clock({16,36},frame*40);
        valid&=view.render(output,frame*40);
    }
    recording=false;
    check(valid&&allocations==0,"live dashboard, DASH/4 and stacked text perform no runtime C++ allocations");
}
