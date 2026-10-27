#include "system.hpp"
#include "platform/linux/framebuffer.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
namespace kafel::console {
static_assert(MAX_NR_CONSOLES == console_count);
static_assert(sizeof(fb_con2fbmap) == 8);
namespace {
[[noreturn]] void error(const char *what) { throw std::system_error(errno, std::generic_category(), what); }
} // namespace
LinuxConsole::LinuxConsole(std::string path) : Console(std::move(path)) {
    fd_ = open(lcd.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd_ < 0)
        error("Open LCD for console routing");
    struct stat st{};
    if (fstat(fd_, &st) < 0 || !S_ISCHR(st.st_mode)) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("LCD is not a character device");
    }
}
LinuxConsole::~LinuxConsole() {
    if (fd_ >= 0)
        close(fd_);
}
Bindings LinuxConsole::bindings() { return framebuffer_bindings("/"); }
void LinuxConsole::bind(std::string_view name, bool value) {
    if (!console_name(name))
        throw std::invalid_argument("Invalid console name");
    const auto path = std::filesystem::path("/sys/class/vtconsole") / name / "bind";
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        error("Open fbcon binding");
    const char byte = value ? '1' : '0';
    ssize_t n;
    do {
        n = ::write(fd, &byte, 1);
    } while (n < 0 && errno == EINTR);
    const int failure = errno;
    close(fd);
    if (n != 1) {
        errno = failure;
        error("Change fbcon binding");
    }
    const auto observed = read_text(path);
    if (observed != std::string(1, byte) && observed != std::string(1, byte) + "\n")
        throw std::runtime_error("Console binding verification failed");
}
Mapping LinuxConsole::maps() {
    Mapping result;
    result.reserve(console_count);
    for (unsigned tty = 1; tty <= console_count; ++tty) {
        fb_con2fbmap mapping{tty, 0};
        if (ioctl(fd_, FBIOGET_CON2FBMAP, &mapping) < 0)
            error("Read console mapping");
        result.push_back(mapping.framebuffer);
    }
    return result;
}
void LinuxConsole::map(unsigned tty, unsigned framebuffer) {
    if (tty < 1 || tty > console_count || framebuffer >= framebuffer_count)
        throw std::invalid_argument("Invalid console mapping");
    fb_con2fbmap mapping{tty, framebuffer};
    if (ioctl(fd_, FBIOPUT_CON2FBMAP, &mapping) < 0)
        error("Set console mapping");
}
std::optional<unsigned> LinuxConsole::hdmi() { return find_hdmi("/", lcd); }
std::string detect_lcd() {
    // Share the dashboard's display validation and auto-selection. No child
    // process, shell, second parser or separate framebuffer selection policy.
    const auto configured = std::getenv("KAFEL_FRAMEBUFFER");
    const auto selected = linux_device::Framebuffer::resolve(configured ? configured : "auto");
    auto canonical = std::filesystem::canonical(selected).string();
    (void)framebuffer_index(canonical);
    return canonical;
}

} // namespace kafel::console
