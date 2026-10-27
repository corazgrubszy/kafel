#include "platform/linux/evdev_touch.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <system_error>
#include <time.h>
#include <unistd.h>

namespace kafel::linux_device {
namespace {
constexpr std::size_t word_bits = sizeof(unsigned long) * 8;
template <std::size_t N> bool bit(const std::array<unsigned long, N> &bits, unsigned code) noexcept {
    return code / word_bits < N && (bits[code / word_bits] & (1UL << (code % word_bits))) != 0;
}
[[noreturn]] void fail(const char *operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
} // namespace
bool EvdevTouch::accept(const char *path, int width, int height) {
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct Close {
        int fd;
        ~Close() {
            if (fd >= 0)
                close(fd);
        }
    } guard{fd};
    std::array<unsigned long, (INPUT_PROP_MAX + word_bits) / word_bits> props{};
    std::array<unsigned long, (KEY_MAX + word_bits) / word_bits> keys{};
    input_absinfo x{}, y{}, slots{};
    if (ioctl(fd, EVIOCGPROP(sizeof(props)), props.data()) < 0 || !bit(props, INPUT_PROP_DIRECT) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0 || !bit(keys, BTN_TOUCH) ||
        ioctl(fd, EVIOCGABS(ABS_X), &x) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &y) < 0 ||
        x.maximum <= x.minimum || y.maximum <= y.minimum)
        return false;
    // MT-only devices need a different adapter. For compatibility devices,
    // monitor every slot rather than letting pointer emulation switch fingers.
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slots) == 0 &&
        (slots.minimum != 0 || slots.maximum >= TouchDecoder::max_slots))
        return false;
    int clock = CLOCK_MONOTONIC;
    if (ioctl(fd, EVIOCSCLOCKID, &clock) < 0)
        fail("set monotonic touch clock");
    decoder_.emplace(TouchAxis{x.minimum, x.maximum}, TouchAxis{y.minimum, y.maximum}, width, height);
    fd_ = fd;
    guard.fd = -1;
    try {
        resync();
    } catch (...) {
        close(fd_);
        fd_ = -1;
        decoder_.reset();
        throw;
    }
    char name[128]{};
    if (ioctl(fd_, EVIOCGNAME(sizeof(name)), name) < 0)
        std::snprintf(name, sizeof(name), "unknown");
    std::fprintf(stderr, "touch: %s (%s), X=%d..%d Y=%d..%d -> %dx%d\n", path, name, x.minimum, x.maximum,
                 y.minimum, y.maximum, width, height);
    return true;
}
EvdevTouch::EvdevTouch(int width, int height, const char *path) {
    if (width < 2 || width > 4096 || height < 2 || height > 4096)
        throw std::invalid_argument("touch screen size");
    if (path) {
        if (!accept(path, width, height))
            throw std::runtime_error("touch device inaccessible or lacks direct ABS_X/Y + BTN_TOUCH");
        return;
    }
    DIR *directory = opendir("/dev/input");
    if (!directory)
        fail("open /dev/input");
    struct CloseDir {
        DIR *dir;
        ~CloseDir() { closedir(dir); }
    } guard{directory};
    while (const auto *entry = readdir(directory)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0)
            continue;
        const char *suffix = entry->d_name + 5;
        if (!*suffix || std::strspn(suffix, "0123456789") != std::strlen(suffix))
            continue;
        char candidate[64];
        if (std::snprintf(candidate, sizeof(candidate), "/dev/input/%s", entry->d_name) >=
            static_cast<int>(sizeof(candidate)))
            continue;
        if (accept(candidate, width, height))
            return;
    }
    throw std::runtime_error("no accessible direct touchscreen with ABS_X/Y + BTN_TOUCH");
}
EvdevTouch::~EvdevTouch() {
    if (fd_ >= 0)
        close(fd_);
}
void EvdevTouch::resync() {
    std::array<unsigned long, (KEY_MAX + word_bits) / word_bits> keys{};
    input_absinfo x{}, y{};
    if (ioctl(fd_, EVIOCGKEY(sizeof(keys)), keys.data()) < 0 || ioctl(fd_, EVIOCGABS(ABS_X), &x) < 0 ||
        ioctl(fd_, EVIOCGABS(ABS_Y), &y) < 0)
        fail("resync touch");
    // A finger already held at startup/after loss must be lifted first.
    decoder_->reset(bit(keys, BTN_TOUCH), x.value, y.value);
}
void EvdevTouch::pump(Sink sink, void *data, std::uint64_t now_ms) {
    if (!sink)
        throw std::invalid_argument("touch sink");
    timespec clock{};
    if (clock_gettime(CLOCK_MONOTONIC, &clock) < 0)
        fail("read monotonic touch clock");
    const auto kernel_now = static_cast<std::uint64_t>(clock.tv_sec) * 1000 + clock.tv_nsec / 1000000;
    std::array<input_event, 32> events{};
    // Bounded work protects rendering if the input producer is very busy.
    for (int batch = 0; batch < 8; ++batch) {
        const auto size = read(fd_, events.data(), sizeof(events));
        if (size < 0 && errno == EINTR) {
            --batch;
            continue;
        }
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (size < 0)
            fail("read touch");
        if (size == 0)
            throw std::runtime_error("touch device disconnected");
        if (size % sizeof(input_event))
            throw std::runtime_error("partial evdev record");
        for (std::size_t i = 0; i < static_cast<std::size_t>(size) / sizeof(input_event); ++i) {
            const auto &event = events[i];
            if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                if (auto sample = decoder_->cancel())
                    sink(data, *sample, timeline_.map(kernel_now, kernel_now, now_ms));
                dropped_ = true;
                continue;
            }
            if (dropped_) {
                if (event.type == EV_SYN && event.code == SYN_REPORT) {
                    resync();
                    dropped_ = false;
                }
                continue;
            }
            using E = TouchDecoder::Event;
            std::optional<E> code;
            if (event.type == EV_ABS) {
                switch (event.code) {
                case ABS_X:
                    code = E::X;
                    break;
                case ABS_Y:
                    code = E::Y;
                    break;
                case ABS_MT_SLOT:
                    code = E::Slot;
                    break;
                case ABS_MT_TRACKING_ID:
                    code = E::TrackingId;
                    break;
                default:
                    break;
                }
            } else if (event.type == EV_KEY && event.code == BTN_TOUCH)
                code = E::Contact;
            else if (event.type == EV_SYN && event.code == SYN_REPORT)
                code = E::Report;
            if (code)
                if (auto sample = decoder_->feed(*code, event.value)) {
                    const auto timestamp =
                        event.time.tv_sec < 0 || event.time.tv_usec < 0 || event.time.tv_usec >= 1000000
                            ? kernel_now
                            : static_cast<std::uint64_t>(event.time.tv_sec) * 1000 +
                                  event.time.tv_usec / 1000;
                    sink(data, *sample, timeline_.map(timestamp, kernel_now, now_ms));
                }
        }
    }
}
} // namespace kafel::linux_device
