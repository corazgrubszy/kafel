#include "dashboard/dashboard_view.hpp"
#include <array>
#include <cstdio>
#include <string_view>
namespace {
bool write_frame(const char* path,const std::array<kafel::Pixel,240*320>& pixels) {
    auto* file=std::fopen(path,"wb"); if(!file) return false;
    std::fputs("P6\n240 320\n255\n",file);
    bool ok=true;
    for(auto pixel:pixels) {
        const unsigned char rgb[]={static_cast<unsigned char>(((pixel>>11)&31)*255/31),static_cast<unsigned char>(((pixel>>5)&63)*255/63),static_cast<unsigned char>((pixel&31)*255/31)};
        ok&=std::fwrite(rgb,1,3,file)==3;
    }
    return std::fclose(file)==0&&ok;
}
}
int main(int argc,char** argv) {
    if(argc<2||argc>4) return 2;
    kafel::DashboardView view;
    kafel::SystemSnapshot system;
    system.hostname.assign("cm4stack"); system.ipv4.assign("192.0.2.220"); system.cpu_percent=37; system.ram_percent=42; system.storage_percent=18; system.storage_emmc=true;
    view.update_system(system); view.update_clock({16,35},0); view.update_date(2026,10,16,5);
    kafel::ServiceRegistry registry;
    const auto message=kafel::parse_service_message("DASH/4\tinfohub\tInfoHub\nTILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\tSucho\tWiatr: 5 m/s\t1529\nTILE\twaste\t2\tok\tattention\tŚmieci\ttext\t[#60A8E8]🗑[/] [#E8C850]🗑[/]\u2028Dzisiaj\n");
    if(!message||!registry.receive(*message,0)||!view.replace_services(registry.tiles().view(),0)) return 1;
    std::array<kafel::Pixel,240*320> pixels{};
    kafel::Surface surface(pixels,240,320,240);
    if(argc==4) {
        using K=kafel::TouchSample::Kind;
        const bool metric=std::string_view(argv[2])=="metric-frames";
        if(metric) {
            view.pointer({K::Down,210,270},100);view.pointer({K::Move,10,270},700);view.pointer({K::Up,10,270},710);
            view.update(1300);
        }
        for(unsigned frame=0;frame<(metric?240U:90U);++frame) {
            const unsigned now=(metric?1400:0)+frame*40;
            if(!metric&&frame==5) view.pointer({K::Down,80,270},now);
            if(!metric&&frame>5&&frame<=10) view.pointer({K::Move,80+static_cast<int>(frame-5)*24,270},now);
            if(!metric&&frame==11) view.pointer({K::Up,200,270},now);
            if(!metric&&frame==30) view.pointer({K::Down,210,270},now);
            if(!metric&&frame>30&&frame<=35) view.pointer({K::Move,210-static_cast<int>(frame-30)*38,270},now);
            if(!metric&&frame==36) view.pointer({K::Up,20,270},now);
            if(!metric&&frame==60) view.pointer({K::Down,20,270},now);
            if(!metric&&frame>60&&frame<=65) view.pointer({K::Move,20+static_cast<int>(frame-60)*38,270},now);
            if(!metric&&frame==66) view.pointer({K::Up,210,270},now);
            view.update(now); view.render(surface,now);
            char path[1024];
            const int length=std::snprintf(path,sizeof(path),"%s/%03u.ppm",argv[3],frame);
            if(length<0||static_cast<std::size_t>(length)>=sizeof(path)||!write_frame(path,pixels)) return 1;
        }
        return 0;
    }
    if(argc==3 && (std::string_view(argv[2])=="service"||std::string_view(argv[2])=="weather")) {
        view.pointer({kafel::TouchSample::Kind::Down,210,270},100);
        const unsigned release=std::string_view(argv[2])=="weather"?700:200;
        view.pointer({kafel::TouchSample::Kind::Move,10,270},release);
        view.pointer({kafel::TouchSample::Kind::Up,10,270},release+10);
    }
    if(!view.render(surface,1400)) return 1;
    return write_frame(argv[1],pixels)?0:1;
}
