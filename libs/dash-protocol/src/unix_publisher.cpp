#include "platform/unix_publisher.hpp"
#include "services/service_status.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace kafel::unix_transport {
std::string default_socket_path() {
    return "/tmp/dashboard-v2-" + std::to_string(getuid()) + "/status.sock";
}
bool valid_socket_path(std::string_view path) noexcept {
    sockaddr_un address{};
    return !path.empty() && path.size() < sizeof address.sun_path && path.find('\0') == path.npos;
}
int publish(std::string_view path, std::string_view packet) noexcept {
    if (packet.empty() || packet.size() > service_packet_bytes) return EMSGSIZE;
    sockaddr_un address{};
    if (!valid_socket_path(path))
        return EINVAL;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.data(), path.size());
    const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return errno;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        const int error = errno;
        close(fd);
        return error;
    }
    const auto size = sendto(fd, packet.data(), packet.size(), 0,
        reinterpret_cast<const sockaddr*>(&address), sizeof address);
    const int result = size < 0 ? errno : static_cast<std::size_t>(size) == packet.size() ? 0 : EIO;
    close(fd);
    return result;
}
}
