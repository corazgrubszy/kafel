#include "service_feed.hpp"
#include "async_compiler.hpp"
#include "dash5/unix.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unistd.h>
namespace kafel {
using namespace dash5;
#ifdef KAFEL_ASYNC_TEST_HOOKS
void before_service_delivery_for_test();
#endif
namespace {
void channel(int (&fds)[2]) {
    if (pipe(fds) < 0)
        throw std::system_error(errno, std::generic_category(), "service pipe");
    for (int fd : fds) {
        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            fds[0] = fds[1] = -1;
            throw std::runtime_error("service pipe flags");
        }
    }
}
std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
} // namespace
struct ServiceFeed::Impl {
    std::optional<AsyncCompiler> compiler;
    Receiver receiver;
    Server server;
    std::shared_ptr<const std::vector<ServiceCard>> cards{std::make_shared<const std::vector<ServiceCard>>()};
    std::shared_ptr<const std::string> failure;
    int wake[2]{-1, -1}, stop[2]{-1, -1}, completed[2]{-1, -1};
    std::thread worker;
    explicit Impl(std::string path)
        : receiver([this](const Snapshot &s, const Resources &r) { return compiler->start(s, r); },
                   [this](const Resource &r) {
                       try {
                           return compiler->validate(r);
                       } catch (const std::bad_alloc &) {
                           throw;
                       } catch (const std::invalid_argument &) {
                           throw std::invalid_argument(r.media_type == "image/svg+xml" ? "invalid_svg"
                                                                                       : "invalid_template");
                       }
                   },
                   [this] { return compiler->complete(); }),
          server(path.empty() ? default_socket_path() : std::move(path), receiver) {
        channel(wake);
        try {
            channel(stop);
            channel(completed);
            compiler.emplace(completed[1]);
            worker = std::thread([this] { run(); });
        } catch (...) {
            for (int fd : wake)
                ::close(fd);
            for (int fd : stop)
                if (fd >= 0)
                    ::close(fd);
            for (int fd : completed)
                if (fd >= 0)
                    ::close(fd);
            throw;
        }
    }
    ~Impl() {
        char c = 1;
        (void)::write(stop[1], &c, 1);
        if (worker.joinable())
            worker.join();
        compiler.reset();
        for (int fd : wake)
            ::close(fd);
        for (int fd : stop)
            ::close(fd);
        for (int fd : completed)
            ::close(fd);
    }
    void notify() {
        char c = 1;
        (void)::write(wake[1], &c, 1);
    }
    void publish_changes(std::uint64_t &generation) {
        if (generation == receiver.generation())
            return;
        auto next = std::make_shared<std::vector<ServiceCard>>();
        for (const auto &e : receiver.entries()) {
            const auto &prepared = e.presentation;
            for (std::size_t i = 0; i < e.snapshot.tiles.size(); ++i) {
                const auto &t = e.snapshot.tiles[i];
                next->push_back(ServiceCard({.owner = e.id,
                                             .id = t.id,
                                             .title = t.title,
                                             .state = t.state,
                                             .message = t.message,
                                             .attention = t.attention,
                                             .offline = e.offline,
                                             .frame = prepared->frames[i]}));
            }
        }
#ifdef KAFEL_ASYNC_TEST_HOOKS
        before_service_delivery_for_test();
#endif
        std::atomic_store(&cards, std::shared_ptr<const std::vector<ServiceCard>>(next));
        generation = receiver.generation();
        notify();
    }
    void run() noexcept {
        try {
            std::uint64_t generation = 0;
            for (;;) {
                // An asynchronous commit may have queued its ACK at the end
                // of the previous iteration. Publish the matching UI snapshot
                // before the transport can flush that ACK.
                publish_changes(generation);
                auto now = now_ms();
                server.pump(now);
                receiver.expire(now);
                publish_changes(generation);
                auto fds = server.descriptors();
                fds.push_back({completed[0], POLLIN, 0});
                fds.push_back({stop[0], POLLIN, 0});
                auto deadline = server.deadline();
                for (const auto &e : receiver.entries())
                    if (!e.offline) {
                        auto due = e.seen + 15000;
                        if (!deadline || due < *deadline)
                            deadline = due;
                    }
                now = now_ms();
                int timeout =
                    deadline ? static_cast<int>(
                                   *deadline > now ? std::min<std::uint64_t>(*deadline - now, 2147483647) : 0)
                             : -1;
                int n = ::poll(fds.data(), fds.size(), timeout);
                if (n < 0 && errno != EINTR)
                    throw std::system_error(errno, std::generic_category(), "service poll");
                if (fds.back().revents & POLLIN)
                    break;
                if (fds[fds.size() - 2].revents & POLLIN) {
                    char byte;
                    ssize_t count;
                    do {
                        count = ::read(completed[0], &byte, 1);
                    } while (count < 0 && errno == EINTR);
                    if (count != 1)
                        throw std::runtime_error("missing preparation completion notification");
                    server.preparation_ready(now_ms());
                }
            }
        } catch (const std::exception &e) {
            std::atomic_store(&failure, std::make_shared<const std::string>(e.what()));
            notify();
        }
    }
};
ServiceFeed::ServiceFeed(std::string path) : impl_(std::make_unique<Impl>(std::move(path))) {}
ServiceFeed::~ServiceFeed() = default;
ServiceFeed::PreparationCosts ServiceFeed::preparation_costs() const noexcept {
    return impl_->compiler->costs();
}
int ServiceFeed::descriptor() const noexcept { return impl_->wake[0]; }
std::shared_ptr<const std::vector<ServiceCard>> ServiceFeed::consume() {
    std::array<char, 128> b{};
    while (::read(impl_->wake[0], b.data(), b.size()) > 0) {
    }
    if (auto error = std::atomic_load(&impl_->failure))
        throw std::runtime_error(*error);
    return std::atomic_load(&impl_->cards);
}
} // namespace kafel
