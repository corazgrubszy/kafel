#include "isolation.hpp"
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
namespace kafel::console {
namespace {
[[noreturn]] void error(const char *message) {
    throw std::system_error(errno, std::generic_category(), message);
}
void safe_file(int fd) {
    struct stat st{};
    if (fstat(fd, &st) < 0)
        error("Console state stat");
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) || st.st_nlink != 1)
        throw std::runtime_error("Unsafe console state file");
}
} // namespace
StateStore::StateStore(std::filesystem::path directory) : directory_(std::move(directory)) {
    if (mkdir(directory_.c_str(), 0700) < 0 && errno != EEXIST)
        error("Create console state directory");
    directory_fd_ = open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory_fd_ < 0)
        error("Open console state directory");
    try {
        struct stat st{};
        if (fstat(directory_fd_, &st) < 0)
            error("Console directory stat");
        if (st.st_uid != geteuid() || (st.st_mode & 077))
            throw std::runtime_error("Unsafe console state directory");
        lock_ = openat(directory_fd_, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        if (lock_ < 0)
            error("Open console state lock");
        safe_file(lock_);
        if (flock(lock_, LOCK_EX | LOCK_NB) < 0)
            error("Console state already locked");
    } catch (...) {
        if (lock_ >= 0)
            close(lock_);
        close(directory_fd_);
        throw;
    }
}
StateStore::~StateStore() {
    if (lock_ >= 0)
        close(lock_);
    if (directory_fd_ >= 0)
        close(directory_fd_);
}
std::optional<std::string> StateStore::read(const char *name) const {
    int fd = openat(directory_fd_, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT)
            return {};
        error("Open console snapshot");
    }
    try {
        safe_file(fd);
        std::string bytes;
        char buffer[1024];
        for (;;) {
            auto n = ::read(fd, buffer, sizeof buffer);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                error("Read console snapshot");
            if (!n)
                break;
            bytes.append(buffer, static_cast<std::size_t>(n));
            if (bytes.size() > 65536)
                throw std::runtime_error("Oversized console snapshot");
        }
        close(fd);
        return bytes;
    } catch (...) {
        close(fd);
        throw;
    }
}
Snapshot StateStore::original() const {
    auto bytes = read("original.json");
    if (!bytes)
        throw std::runtime_error("No original console snapshot");
    return decode_snapshot(*bytes);
}
void StateStore::save(const char *name, std::string_view bytes) {
    // The directory is private and held open; do not replace a symlink, shared
    // file or unexpectedly accessible state left by another program.
    (void)read(name);
    std::string temporary = (directory_ / (std::string(".") + name + ".XXXXXX")).string();
    int fd = mkstemp(temporary.data());
    if (fd < 0)
        error("Create console state temporary");
    try {
        if (fchmod(fd, 0600) < 0)
            error("Console state mode");
        while (!bytes.empty()) {
            auto n = ::write(fd, bytes.data(), bytes.size());
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                error("Write console state");
            if (!n)
                throw std::runtime_error("Console state write made no progress");
            bytes.remove_prefix(static_cast<std::size_t>(n));
        }
        if (fsync(fd) < 0)
            error("Sync console state");
        if (rename(temporary.c_str(), (directory_ / name).c_str()) < 0)
            error("Publish console state");
        if (fsync(directory_fd_) < 0 && errno != EINVAL)
            error("Sync console state directory");
        close(fd);
    } catch (...) {
        close(fd);
        unlink(temporary.c_str());
        throw;
    }
}
void StateStore::save_original(const Snapshot &state) {
    if (auto bytes = read("original.json")) {
        if (decode_snapshot(*bytes).lcd != state.lcd)
            throw std::runtime_error("Original snapshot belongs to another LCD");
        return;
    }
    save("original.json", encode(state));
}
void StateStore::save_report(const Report &report) { save("status.json", encode(report)); }
void StateStore::clear() {
    // Verify both files before removing either one.
    (void)read("original.json");
    (void)read("status.json");
    for (auto name : {"original.json", "status.json"})
        if (unlinkat(directory_fd_, name, 0) < 0 && errno != ENOENT)
            error("Remove console state");
}
Report isolate_and_record(Console &console, StateStore &store) {
    const auto before = snapshot(console);
    store.save_original(before);
    const auto report = isolate(console, before);
    try {
        store.save_report(report);
    } catch (const std::exception &failure) {
        const std::string primary = failure.what();
        try {
            restore(console, before);
        } catch (const std::exception &rollback) {
            throw std::runtime_error(primary + "; " + rollback.what());
        }
        throw;
    }
    return report;
}
} // namespace kafel::console
