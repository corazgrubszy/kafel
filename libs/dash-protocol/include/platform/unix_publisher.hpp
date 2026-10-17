#pragma once
#include <string>
#include <string_view>
namespace kafel::unix_transport {
// A nonblocking datagram per snapshot; dashboard restarts need no reconnect logic.
// Returns an errno value, zero on success. Never owns/unlinks the receiver path.
bool valid_socket_path(std::string_view path) noexcept;
int publish(std::string_view path, std::string_view packet) noexcept;
std::string default_socket_path();
}
