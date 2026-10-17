#include "dashboard/dashboard_view.hpp"
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

namespace {
using View=kafel::DashboardView;
using Kind=kafel::TouchSample::Kind;
constexpr unsigned frame_count=150,frame_ms=40;
constexpr std::string_view weather=
    "DASH/4\tkafel-weather\tKafel Weather\n"
    "TILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\tSucho\tWiatr: 5 m/s\t1529\n";
constexpr std::string_view waste=
    "DASH/4\tkafel-waste\tKafel Waste\n"
    "TILE\twaste\t1\tok\tattention\tŚmieci\ttext\t[#0072CE]🗑[/][#E6B800]🗑[/]\u2028Dzisiaj\n";

bool prepare(View& view,kafel::ServiceRegistry& registry,std::string_view scene) {
    kafel::SystemSnapshot system;
    system.hostname.assign("kafel-demo");system.ipv4.assign("192.0.2.220");
    system.cpu_percent=37;system.ram_percent=42;system.storage_percent=18;system.storage_emmc=true;
    view.update_system(system);view.update_clock({16,35},0);view.update_date(2026,10,17,6);
    auto receive=[&](std::string_view wire) {
        const auto message=kafel::parse_service_message(wire);
        return message&&registry.receive(*message,0).has_value();
    };
    if(scene!="waste"&&!receive(weather)) return false;
    return receive(waste)&&view.replace_services(registry.tiles().view(),0);
}

bool write_frame(const std::string& path,const std::array<kafel::Pixel,240*320>& pixels) {
    auto* file=std::fopen(path.c_str(),"wb");if(!file) return false;
    bool ok=std::fputs("P6\n240 320\n255\n",file)>=0;
    for(auto pixel:pixels) {
        const unsigned char rgb[]={static_cast<unsigned char>(((pixel>>11)&31)*255/31),
            static_cast<unsigned char>(((pixel>>5)&63)*255/63),static_cast<unsigned char>((pixel&31)*255/31)};
        ok&=std::fwrite(rgb,1,3,file)==3;
    }
    return std::fclose(file)==0&&ok;
}

void swipe_to_services(View& view) {
    view.pointer({Kind::Down,210,270},100);
    view.pointer({Kind::Move,10,270},700);
    view.pointer({Kind::Up,10,270},710);
    view.update(1400);
}

bool screenshot(const std::string& path,std::string_view scene) {
    View view;kafel::ServiceRegistry registry;
    if(!prepare(view,registry,scene)) return false;
    if(scene!="system") swipe_to_services(view);
    std::array<kafel::Pixel,240*320> pixels{};
    kafel::Surface surface(pixels,240,320,240);
    return view.render(surface,1400)&&write_frame(path,pixels);
}

bool animation(const std::string& directory) {
    View view;kafel::ServiceRegistry registry;
    if(!prepare(view,registry,"system")) return false;
    std::array<kafel::Pixel,240*320> pixels{};
    kafel::Surface surface(pixels,240,320,240);
    for(unsigned frame=0;frame<frame_count;++frame) {
        const unsigned now=1000+frame*frame_ms;
        // Synthetic input enters the same controller as real evdev gestures.
        if(frame==25) view.pointer({Kind::Down,210,270},now);
        if(frame>25&&frame<=40) view.pointer({Kind::Move,210-static_cast<int>((frame-25)*200/15),270},now);
        if(frame==41) view.pointer({Kind::Up,10,270},now);
        if(frame==100) view.pointer({Kind::Down,10,270},now);
        if(frame>100&&frame<=115) view.pointer({Kind::Move,10+static_cast<int>((frame-100)*200/15),270},now);
        if(frame==116) view.pointer({Kind::Up,210,270},now);
        view.update(now);
        char name[32];std::snprintf(name,sizeof(name),"/frame-%03u.ppm",frame);
        if(!view.render(surface,now)||!write_frame(directory+name,pixels)) return false;
    }
    return true;
}
}

int main(int argc,char** argv) {
    if(argc<2||argc>3) return 2;
    if(std::string_view(argv[1])=="--assets") {
        if(argc!=3) return 2;
        const std::string directory=argv[2];
        return screenshot(directory+"/dashboard.ppm","system")&&
            screenshot(directory+"/weather.ppm","weather")&&
            screenshot(directory+"/waste.ppm","waste")&&animation(directory)?0:1;
    }
    const auto scene=argc==3?std::string_view(argv[2]):std::string_view("system");
    if(scene!="system"&&scene!="weather"&&scene!="waste") return 2;
    return screenshot(argv[1],scene)?0:1;
}
