#include "config.hpp"
#include <charconv>
#include <stdexcept>
namespace kafel::app {
namespace {
void path(std::string_view value, const char *name) {
    if (value.empty() || value.front() != '/' || value.size() > 4095 ||
        value.find_first_of("\r\n\t") != value.npos || value.find('\0') != value.npos)
        throw std::invalid_argument(std::string("invalid ") + name + " path");
}
std::uint64_t seconds(std::string_view value) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        result > 86400)
        throw std::invalid_argument("idle timeout must be 0..86400 seconds");
    return result * 1000;
}
} // namespace
DashboardConfig parse_config(std::span<const std::string_view> arguments, Environment environment) {
    DashboardConfig config;
    for (auto arg : arguments) {
        Command command = Command::Run;
        if (arg == "--help")
            command = Command::Help;
        else if (arg == "--version")
            command = Command::Version;
        else if (arg == "--check")
            command = Command::Check;
        else if (arg == "--check-input")
            command = Command::CheckInput;
        else if (arg == "--system-info")
            command = Command::SystemInfo;
        else if (arg == "--display-info")
            command = Command::DisplayInfo;
        else
            continue;
        if (config.command != Command::Run)
            throw std::invalid_argument("choose one command");
        config.command = command;
    }
    std::string idle = "60";
    if (environment && config.command != Command::Version && config.command != Command::Help) {
        auto read = [&](const char *name, std::string &value) {
            if (const char *p = environment(name))
                value = p;
        };
        read("KAFEL_FRAMEBUFFER", config.framebuffer);
        read("KAFEL_TOUCH", config.touch);
        read("KAFEL_DASHBOARD_SOCKET", config.socket);
        read("KAFEL_IDLE_TIMEOUT", idle);
    }
    for (auto arg : arguments) {
        if (arg.starts_with("--fb="))
            config.framebuffer = arg.substr(5);
        else if (arg.starts_with("--touch="))
            config.touch = arg.substr(8);
        else if (arg.starts_with("--socket=")) {
            if (arg.size() == 9)
                throw std::invalid_argument("empty socket path");
            config.socket = arg.substr(9);
        } else if (arg.starts_with("--idle-timeout="))
            idle = arg.substr(15);
        else if (arg != "--help" && arg != "--version" && arg != "--check" && arg != "--check-input" &&
                 arg != "--system-info" && arg != "--display-info")
            throw std::invalid_argument("unknown argument: " + std::string(arg));
    }
    if (config.command == Command::Version || config.command == Command::Help)
        return config;
    if (config.framebuffer != "auto")
        path(config.framebuffer, "framebuffer");
    if (config.touch != "auto" && config.touch != "off")
        path(config.touch, "touch");
    if (!config.socket.empty()) {
        path(config.socket, "socket");
        if (config.socket.size() >= 108 || config.socket.back() == '/' || config.socket.ends_with("/.") ||
            config.socket.ends_with("/..") || config.socket.ends_with("/lock"))
            throw std::invalid_argument("invalid Unix socket path");
    }
    config.idle_timeout_ms = seconds(idle);
    return config;
}
} // namespace kafel::app
