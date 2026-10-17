#include "platform/linux/service_socket.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace {
void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
struct Directory {
    char path[40]="/tmp/cm4-services-test-XXXXXX";
    const char* filename="status.sock";
    Directory() { check(mkdtemp(path),"mkdtemp"); }
    ~Directory() { std::filesystem::remove_all(path); }
    std::string socket() const { return std::string(path)+"/"+filename; }
};
void send(const Directory& directory,std::string_view wire) {
    const int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0); check(fd>=0,"sender socket");
    sockaddr_un address{}; address.sun_family=AF_UNIX;
    const auto path=directory.socket(); std::memcpy(address.sun_path,path.c_str(),path.size()+1);
    const auto n=sendto(fd,wire.data(),wire.size(),0,reinterpret_cast<sockaddr*>(&address),sizeof(address));
    close(fd); check(n==static_cast<ssize_t>(wire.size()),"send datagram");
}
void count(void* data,const kafel::ServiceMessage& message) {
    check(message.id.view()=="test","received expected ID");
    auto& received=*static_cast<unsigned*>(data);
    check(message.tile_count==1&&message.tiles[0].attention==(received==0),"tile attention survives transport");
    ++received;
}
template<class F> void rejected(F action,const char* text) {
    bool failed=false; try { action(); } catch(const std::exception&) { failed=true; } check(failed,text);
}
}
int main() {
    using kafel::linux_device::ServiceSocket;
    try {
        Directory directory;
        unsigned received=0;
        {
            ServiceSocket inbox(directory.path);
            check(fcntl(inbox.descriptor(),F_GETFL)&O_NONBLOCK,"nonblocking inbox");
            check(fcntl(inbox.descriptor(),F_GETFD)&FD_CLOEXEC,"close-on-exec inbox");
            struct stat metadata{}; check(stat(directory.socket().c_str(),&metadata)==0 && (metadata.st_mode&0777)==0600,"socket permissions");
            rejected([&] { ServiceSocket duplicate(directory.path); },"second receiver must not replace live socket");
            inbox.pump(count,&received,0); check(received==0,"empty pump does not block");
            send(directory,"DASH/4\ttest\tTest\nTILE\tstatus\t3\tok\tattention\tTest\ttext\tDziała\n");
            send(directory,"invalid"); send(directory,std::string(kafel::service_packet_bytes+1,'x'));
            send(directory,"DASH/2\ttest\tTest\tok\tUnsupported\n");
            inbox.pump(count,&received,0); check(received==1 && inbox.rejected_packets()==3,"invalid, truncated and unsupported-version packets rejected");
            send(directory,"DASH/4\ttest\tTest\nTILE\tstatus\t3\tok\tnormal\tTest\ttext\tDziała\n"); inbox.pump(count,&received,1);
            check(received==2,"valid delivery still works after bad packets and duplicate receiver");
            send(directory,"DASH/4\ttest\tTest\nTILE\tweather\t2\twarning\tnormal\tPogoda\tmetric\t-3.5\t°C\tPogodnie\tWiatr: 5 m/s\nTILE\twaste\t3\tok\tattention\tOdbiór\ttext\tJutro\n");
            unsigned multi=0;
            inbox.pump([](void* data,const kafel::ServiceMessage& message) {
                check(message.tile_count==2&&message.tiles[0].span==2&&message.tiles[0].metric&&message.tiles[0].metric->value.view()=="-3.5"&&message.tiles[1].attention,
                      "DASH/4 complete snapshot survives Unix transport");
                ++*static_cast<unsigned*>(data);
            },&multi,2);
            check(multi==1,"exactly one atomic multi-tile datagram delivered");
        }
        check(!std::filesystem::exists(directory.socket()),"destructor removes inbox");
        rejected([&] {ServiceSocket invalid(directory.path,"lock");},"socket cannot overwrite lock");
        rejected([&] {ServiceSocket invalid(directory.path,"../outside");},"socket filename cannot escape directory");
        directory.filename="custom.sock";
        { ServiceSocket custom(directory.path,directory.filename);
          send(directory,"DASH/4\ttest\tTest\nTILE\tstatus\t3\tok\tnormal\tTest\ttext\tCustom endpoint\n");
          custom.pump(count,&received,2); }
        check(received==3&&!std::filesystem::exists(directory.socket()),"custom socket receives and cleans up");
        directory.filename="status.sock";
        // A crashed receiver leaves a pathname, but releases its process lock.
        const auto child=fork(); check(child>=0,"fork");
        if(child==0) { ServiceSocket crashed(directory.path); _exit(0); }
        int status=0; check(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"crashed fixture exits");
        check(std::filesystem::exists(directory.socket()),"stale socket fixture");
        { ServiceSocket restarted(directory.path); send(directory,"DASH/4\ttest\tTest\nTILE\tstatus\t3\tok\tnormal\tTest\ttext\tPo restarcie\n"); restarted.pump(count,&received,2); }
        check(received==4,"restart reclaims stale socket and receives data");
        { const int fd=open(directory.socket().c_str(),O_CREAT|O_WRONLY,0600); check(fd>=0,"fixture file"); close(fd); }
        rejected([&] { ServiceSocket bad(directory.path); },"regular file must not be removed");
        check(std::filesystem::is_regular_file(directory.socket()),"unexpected file retained");
        std::filesystem::remove(directory.socket());
        check(chmod(directory.path,0755)==0,"directory fixture mode");
        rejected([&] { ServiceSocket bad(directory.path); },"public inbox directory rejected");
        check(chmod(directory.path,0700)==0,"restore directory mode");
        const auto link=std::string(directory.path)+"-link"; check(symlink(directory.path,link.c_str())==0,"symlink fixture");
        rejected([&] { ServiceSocket bad(link.c_str()); },"symlink directory rejected"); unlink(link.c_str());
        std::puts("PASS: Linux inbox delivery, validation, locking, crash/restart, cleanup and permissions");
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
