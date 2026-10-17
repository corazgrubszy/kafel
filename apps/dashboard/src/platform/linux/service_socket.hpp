#pragma once
#include "services/service_registry.hpp"
namespace kafel::linux_device {
// A private same-user Unix datagram inbox, nonblocking and bounded per pump.
class ServiceSocket {
public:
    using Sink=void (*)(void*,const ServiceMessage&);
    explicit ServiceSocket(const char* directory=nullptr,const char* filename="status.sock");
    ~ServiceSocket();
    ServiceSocket(const ServiceSocket&)=delete;
    ServiceSocket& operator=(const ServiceSocket&)=delete;
    int descriptor() const noexcept { return fd_; }
    void pump(Sink,void*,std::uint64_t now_ms);
    std::uint64_t rejected_packets() const noexcept { return rejected_; }
private:
    int fd_=-1, lock_fd_=-1;
    std::uint64_t rejected_=0,last_report_ms_=0;
    std::array<char,108> path_{};
};
}
