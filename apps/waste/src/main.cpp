#include "schedule.hpp"
#include "packet.hpp"
#include "platform/unix_publisher.hpp"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
namespace {
volatile std::sig_atomic_t stopping=0;
void stop(int){stopping=1;}
const char* environment(const char* name,const char* fallback="") {
    const auto* value=std::getenv(name);return value?value:fallback;
}
}
int main(int argc,char** argv) {
    using namespace kafel::waste;
    using namespace kafel::unix_transport;
    if(argc==2&&std::string_view(argv[1])=="--version") {std::puts("kafel-waste 0.1.0 DASH/4");return 0;}
    const bool check=argc==3&&std::string_view(argv[1])=="--check";
    if(argc!=1&&!check) {std::fprintf(stderr,"usage: kafel-waste [--version|--check FILE]\n");return 2;}
    const char* path=check?argv[2]:environment("KAFEL_WASTE_SCHEDULE");
    if(!*path){std::fprintf(stderr,"Set KAFEL_WASTE_SCHEDULE\n");return 2;}
    unsigned span=1;
    const std::string_view width=environment("KAFEL_WASTE_SPAN","1");
    if(width!="1"&&width!="2"){std::fprintf(stderr,"KAFEL_WASTE_SPAN must be 1 or 2\n");return 2;}
    span=static_cast<unsigned>(width[0]-'0');
    const auto default_path=default_socket_path();
    const std::string socket=environment("KAFEL_DASHBOARD_SOCKET",default_path.c_str());
    if(!valid_socket_path(socket)){std::fprintf(stderr,"Invalid dashboard socket\n");return 2;}
    if(check) {
        ScheduleCache cache;
        if(const auto error=cache.reload(path)) { std::fprintf(stderr,"%s\n",error->message.c_str());return 1; }
        const auto* schedule=cache.get();
        std::printf("Schedule validated: %zu pickup days\n",schedule->days.size());return 0;
    }
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    using Clock=std::chrono::steady_clock;
    ScheduleCache cache;
    bool publisher_failed=false;
    auto reload_at=Clock::now(),report_at=reload_at;
    std::fputs("READY kafel-waste: publisher started (DASH/4)\n",stderr);
    while(!stopping) {
        auto now=Clock::now();
        if(now>=reload_at) {
            const auto error=cache.reload(path);
            if(!error)std::fprintf(stderr,"waste schedule loaded: %zu pickup days\n",cache.get()->days.size());
            else std::fprintf(stderr,"waste reload failed: %s\n",error->message.c_str());
            reload_at=Clock::now()+std::chrono::seconds(error?30:60);
        }
        const auto wall=std::time(nullptr);const auto today=wall==static_cast<std::time_t>(-1)?std::optional<Date>{}:local_date(wall);
        const auto packet=dashboard_packet(today?cache.get():nullptr,today.value_or(Date{}),wall,cache.degraded(),span);
        const int error=publish(socket,packet);
        now=Clock::now();
        if(error&&(!publisher_failed||now>=report_at)) {
            std::fprintf(stderr,"dashboard publish unavailable: %s\n",std::strerror(error));report_at=now+std::chrono::minutes(1);
        }else if(!error&&publisher_failed)std::fprintf(stderr,"dashboard publish restored\n");
        publisher_failed=error!=0;
        // Poll wakes on signals; wall-clock dates are recomputed each heartbeat.
        if(!stopping)poll(nullptr,0,5000);
    }
}
