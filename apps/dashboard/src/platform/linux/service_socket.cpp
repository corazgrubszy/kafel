#include "platform/linux/service_socket.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <stdexcept>
namespace kafel::linux_device {
namespace { void fail(const char* text) { throw std::system_error(errno,std::generic_category(),text); } }
ServiceSocket::ServiceSocket(const char* requested_directory,const char* filename) {
    if(!*filename||std::strchr(filename,'/')||std::strcmp(filename,".")==0||std::strcmp(filename,"..")==0||std::strcmp(filename,"lock")==0)
        throw std::invalid_argument("invalid service socket filename");
    char default_directory[80];
    std::snprintf(default_directory,sizeof(default_directory),"/tmp/dashboard-v2-%lu",static_cast<unsigned long>(getuid()));
    const char* directory=requested_directory?requested_directory:default_directory;
    if(std::snprintf(path_.data(),path_.size(),"%s/%s",directory,filename)>=static_cast<int>(path_.size()))
        throw std::invalid_argument("service socket path too long");
    if(mkdir(directory,0700)<0 && errno!=EEXIST) fail("create service inbox directory");
    struct stat metadata{};
    if(lstat(directory,&metadata)<0) fail("stat service inbox directory");
    if(!S_ISDIR(metadata.st_mode) || metadata.st_uid!=getuid() || (metadata.st_mode&077)!=0)
        throw std::runtime_error("service inbox directory must be private and owned by dashboard user");
    char lock_path[108]; std::snprintf(lock_path,sizeof(lock_path),"%s/lock",directory);
    lock_fd_=open(lock_path,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock_fd_<0) fail("open service inbox lock");
    bool bound=false;
    try {
        if(fstat(lock_fd_,&metadata)<0) fail("stat service inbox lock");
        if(!S_ISREG(metadata.st_mode) || metadata.st_uid!=getuid() || (metadata.st_mode&077)!=0)
            throw std::runtime_error("invalid service inbox lock");
        if(flock(lock_fd_,LOCK_EX|LOCK_NB)<0) fail("service inbox already in use");
        if(lstat(path_.data(),&metadata)==0) {
            if(!S_ISSOCK(metadata.st_mode) || metadata.st_uid!=getuid()) throw std::runtime_error("unexpected service inbox file");
            if(unlink(path_.data())<0) fail("remove stale service socket");
        } else if(errno!=ENOENT) fail("stat service socket");
        fd_=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        if(fd_<0) fail("create service socket");
        sockaddr_un address{}; address.sun_family=AF_UNIX; std::memcpy(address.sun_path,path_.data(),path_.size());
        if(bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0) fail("bind service socket");
        bound=true;
        if(chmod(path_.data(),0600)<0) fail("chmod service socket");
    } catch(...) {
        if(fd_>=0) close(fd_);
        if(bound) unlink(path_.data());
        close(lock_fd_); fd_=lock_fd_=-1;
        throw;
    }
    std::fprintf(stderr,"services: listening on %s (DASH/4)\n",path_.data());
}
ServiceSocket::~ServiceSocket() {
    if(fd_>=0) { close(fd_); unlink(path_.data()); }
    if(lock_fd_>=0) close(lock_fd_);
}
void ServiceSocket::pump(Sink sink,void* context,std::uint64_t now_ms) {
    for(unsigned i=0;i<16;++i) {
        std::array<char,service_packet_bytes+1> bytes{};
        const auto size=recv(fd_,bytes.data(),bytes.size(),MSG_TRUNC);
        if(size<0) {
            if(errno==EAGAIN || errno==EWOULDBLOCK) return;
            if(errno==EINTR) continue;
            fail("receive service status");
        }
        const auto message=size>static_cast<ssize_t>(service_packet_bytes)?std::nullopt:parse_service_message({bytes.data(),static_cast<std::size_t>(size)});
        if(message) sink(context,*message);
        else {
            ++rejected_;
            if(rejected_==1 || (now_ms>=last_report_ms_ && now_ms-last_report_ms_>=60000)) {
                std::fprintf(stderr,"services: invalid packets rejected=%llu\n",static_cast<unsigned long long>(rejected_));
                last_report_ms_=now_ms;
            }
        }
    }
}
}
