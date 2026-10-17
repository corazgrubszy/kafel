#include "dashboard/dashboard_view.hpp"
#include "services/service_registry.hpp"
#include "platform/linux/service_socket.hpp"
#include "app/frame_schedule.hpp"
#include <poll.h>
#include <cerrno>
#include <system_error>
#include "platform/linux/framebuffer.hpp"
#include "platform/linux/evdev_touch.hpp"
#include "platform/linux/linux_system_probe.hpp"
#include <ctime>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <thread>
#include "app/config.hpp"
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
static_assert(kafel::service_tile_capacity<=ui::RibbonLayout::capacity-3);
struct Services {
    kafel::ServiceRegistry& registry;
    kafel::DashboardView& view;
    std::uint64_t now_ms=0, rejected=0, reported=0;
};
void service(void* data,const kafel::ServiceMessage& message) {
    auto& state=*static_cast<Services*>(data);
    const auto update=state.registry.receive(message,state.now_ms);
    if(!update) {
        ++state.rejected;
        if(state.rejected==1 || state.now_ms-state.reported>=60000) {
            std::fprintf(stderr,"services: registry full, rejected=%llu\n",static_cast<unsigned long long>(state.rejected));
            state.reported=state.now_ms;
        }
        return;
    }
    if(update->changed && !state.view.replace_services(state.registry.tiles().view(),state.now_ms))
        throw std::runtime_error("service registry/view capacity mismatch");
    if(update->added) std::fprintf(stderr,"services: registered %s\n",message.id.bytes.data());

}
void touch(void* data, kafel::TouchSample sample, std::uint64_t now_ms) {
    auto& view=*static_cast<kafel::DashboardView*>(data);
    view.pointer(sample,now_ms);
    using Kind=kafel::TouchSample::Kind;
    if (sample.kind==Kind::Down || sample.kind==Kind::Up || sample.kind==Kind::Cancel)
        std::fprintf(stderr,"touch: %s %d,%d services=%s page=%zu\n",
            sample.kind==Kind::Down?"down":sample.kind==Kind::Up?"up":"cancel",
            sample.x,sample.y,view.selected_tile()>=3?"service":"system",view.selected_tile()+1);
}
}
int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> arguments;
        for(int i=1;i<argc;++i) arguments.emplace_back(argv[i]);
        const auto config=kafel::app::parse_config(arguments,[](const char* name)->const char* {return std::getenv(name);});
        using Command=kafel::app::Command;
        if(config.command==Command::Version) {std::puts("kafel-dashboard 0.3.0-dev");return 0;}
        if(config.command==Command::Help) {
            std::puts("usage: kafel-dashboard [--check|--check-input|--system-info|--display-info|--version]\n"
                      "  --fb=auto|/dev/fbN --touch=auto|/dev/input/eventN|off\n"
                      "  --socket=/absolute/path --idle-timeout=0..86400 (seconds; 0 disables return)");
            return 0;
        }
        if(config.command==Command::DisplayInfo) {
            const auto path=kafel::linux_device::Framebuffer::resolve(config.framebuffer.c_str());
            // The privileged console helper consumes this single absolute path.
            if(path.find_first_of("\r\n")!=path.npos) throw std::runtime_error("invalid framebuffer path");
            std::puts(path.c_str());return 0;
        }
        std::signal(SIGTERM,stop); std::signal(SIGINT,stop);
        kafel::linux_device::LinuxSystemProbe probe;
        if(config.command==Command::SystemInfo) {
            probe.sample(0); std::this_thread::sleep_for(std::chrono::seconds(1));
            const auto snapshot=probe.sample(1000);
            kafel::SystemDisplay display; display.update(snapshot);
            char temperature[16]="--";
            if(snapshot.temperature_millidegrees) std::snprintf(temperature,sizeof(temperature),"%d",*snapshot.temperature_millidegrees);
            std::printf("hostname=%s\nip=%s\ncpu=%s\nram=%s\nstorage=%s\nfree=%s\ntemperature_mC=%s\n",
                snapshot.hostname.bytes.data(),snapshot.ipv4.bytes.data(),display.cpu.bytes.data(),display.ram.bytes.data(),
                snapshot.storage_emmc?"eMMC":"DISK",display.free_space.bytes.data(),temperature);
            return 0;
        }
        auto open_input=[&]() -> std::unique_ptr<kafel::linux_device::EvdevTouch> {
            if(config.touch=="off") return {};
            return std::make_unique<kafel::linux_device::EvdevTouch>(240,320,config.touch=="auto"?nullptr:config.touch.c_str());
        };
        if (config.command==Command::CheckInput) {
            const auto input=open_input();
            std::puts(config.touch=="off"?"Touchscreen disabled by configuration":"Touchscreen validated (no input grabbed, no pixels written)"); return 0;
        }
        kafel::linux_device::Framebuffer framebuffer(config.framebuffer.c_str());
        if (config.command==Command::Check) { std::puts("Framebuffer validated: 240x320 RGB565 (no pixels written)"); return 0; }
        auto input=open_input();
        std::array<kafel::Pixel,240*320> pixels{};
        kafel::Surface surface(pixels,240,320,240);
        // LCD navigation is exclusively physical; no scripted gestures or resets.
        kafel::ServiceRegistry registry;
        kafel::DashboardView view(config.idle_timeout_ms);
        const auto socket_path=std::filesystem::path(config.socket);
        const auto directory=socket_path.parent_path().string(),filename=socket_path.filename().string();
        kafel::linux_device::ServiceSocket inbox(config.socket.empty()?nullptr:directory.c_str(),config.socket.empty()?"status.sock":filename.c_str());
        Services services{registry,view};
        view.update_system(probe.sample(0));
        std::uint64_t sampled_at=0;
        const auto started=std::chrono::steady_clock::now();
        kafel::app::FrameSchedule schedule;
        bool ready=false;
        while (!stopping) {
            const auto now=std::chrono::steady_clock::now();
            const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count());
            if(input) input->pump(touch,&view,elapsed);
            services.now_ms=elapsed; inbox.pump(service,&services,elapsed);
            for(std::size_t i=0;i<registry.size();++i)
                if(registry.expire(i,elapsed)) view.replace_services(registry.tiles().view(),elapsed);
            const auto was_selected=view.selected_tile();
            if(elapsed-sampled_at>=1000) { view.update_system(probe.sample(elapsed)); sampled_at=elapsed; }
            const auto wall=std::time(nullptr); std::tm local{};
            if(wall!=static_cast<std::time_t>(-1) && localtime_r(&wall,&local)) {
                view.update_clock({static_cast<unsigned>(local.tm_hour),static_cast<unsigned>(local.tm_min)},elapsed);
                view.update_date(static_cast<unsigned>(local.tm_year+1900),static_cast<unsigned>(local.tm_mon+1),
                                 static_cast<unsigned>(local.tm_mday),static_cast<unsigned>(local.tm_wday));
            }
            view.update(elapsed);
            const bool dirty=view.needs_redraw(elapsed);
            if(schedule.frame_due(elapsed,dirty)) {
                if (!view.render(surface,elapsed)) throw std::runtime_error("dashboard render failed");
                if (was_selected && !view.selected_tile()) std::fputs("navigation: ribbon idle return\n",stderr);
                framebuffer.present(pixels);
                if (!ready) { std::puts("READY kafel-dashboard: first frame presented (configured input, live system, DASH/4 services)"); std::fflush(stdout); ready=true; }
                const auto presented=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());
                schedule.presented(presented);
            }
            const auto waiting=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());
            pollfd events[]={{input?input->descriptor():-1,POLLIN,0},{inbox.descriptor(),POLLIN,0}};
            const int timeout=schedule.wait_ms(waiting,view.needs_redraw(waiting),sampled_at+1000,view.idle_deadline(),view.next_visual_change(waiting));
            if(stopping) break;
            const int result=::poll(events,2,timeout);
            if(result<0 && errno!=EINTR) throw std::system_error(errno,std::generic_category(),"poll dashboard inputs");
            if(result>0) for(const auto& event:events)
                if(event.revents&(POLLERR|POLLHUP|POLLNVAL)) throw std::runtime_error("dashboard input disconnected");
        }
    } catch (const std::invalid_argument& error) { std::fprintf(stderr,"dashboard configuration: %s\n",error.what()); return 2; }
    catch (const std::exception& error) { std::fprintf(stderr,"dashboard: %s\n",error.what()); return 1; }
}
