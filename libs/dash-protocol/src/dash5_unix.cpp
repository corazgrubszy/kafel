#include "dash5/unix.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
namespace kafel::dash5 {
namespace {
[[noreturn]] void error(const char *what) { throw std::system_error(errno, std::generic_category(), what); }
void nonblocking(int fd) {
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) < 0)
        error("socket SIGPIPE policy");
#endif
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        error("nonblocking socket");
}
sockaddr_un address(std::string_view path) {
    if (!valid_socket_path(path))
        throw std::invalid_argument("socket path");
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.data(), path.size());
    return a;
}
using Clock = std::chrono::steady_clock;
void wait(int fd, short events, Clock::time_point until) {
    for (;;) {
        auto left = std::chrono::ceil<std::chrono::milliseconds>(until - Clock::now()).count();
        if (left <= 0) {
            errno = ETIMEDOUT;
            error("DASH/5 deadline");
        }
        pollfd p{fd, events, 0};
        int n = ::poll(&p, 1, static_cast<int>(left));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            error("poll");
        if (n && p.revents & events)
            return;
        if (n && p.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            errno = ECONNRESET;
            error("peer closed");
        }
    }
}
void send_all(int fd, std::string_view bytes, Clock::time_point until) {
    while (!bytes.empty()) {
        auto n = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (n > 0)
            bytes.remove_prefix(static_cast<std::size_t>(n));
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            wait(fd, POLLOUT, until);
        else if (n < 0 && errno == EINTR)
            continue;
        else
            error("send DASH/5");
    }
}
std::string receive(int fd, std::size_t size, Clock::time_point until) {
    std::string s(size, '\0');
    std::size_t done = 0;
    while (done < size) {
        auto n = ::recv(fd, s.data() + done, size - done, 0);
        if (n > 0)
            done += static_cast<std::size_t>(n);
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            wait(fd, POLLIN, until);
        else if (n < 0 && errno == EINTR)
            continue;
        else {
            errno = ECONNRESET;
            error("read DASH/5");
        }
    }
    return s;
}
Json response(int fd, Clock::time_point until) {
    auto header = receive(fd, 4, until);
    std::size_t n = 0;
    for (auto c : header)
        n = n * 256 + static_cast<unsigned char>(c);
    if (n < 1 || n > frame_limit)
        throw std::invalid_argument("response length");
    return parse(receive(fd, n, until));
}
bool same_uid(int fd) {
#ifdef __linux__
    ucred cred{};
    socklen_t n = sizeof cred;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &n) == 0 && cred.uid == getuid();
#else
    uid_t uid;
    gid_t gid;
    return getpeereid(fd, &uid, &gid) == 0 && uid == getuid();
#endif
}
} // namespace
std::string default_socket_path() { return "/tmp/kafel-" + std::to_string(getuid()) + "/dashboard.sock"; }
bool valid_socket_path(std::string_view p) noexcept {
    return p.size() > 1 && p.front() == '/' && p.size() < sizeof(sockaddr_un{}.sun_path) &&
           p.find_first_of("\r\n\t\0", 0, 4) == p.npos;
}
Publisher::Publisher(std::string path, std::string id, std::string name, Resources resources)
    : path_(std::move(path)), id_(std::move(id)), name_(std::move(name)), instance_(random_instance()),
      resources_(std::move(resources)) {
    if (!valid_socket_path(path_))
        throw std::invalid_argument("socket path");
}
Publisher::~Publisher() { close(); }
void Publisher::close() noexcept {
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}
void Publisher::connect() {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0)
        error("socket");
    nonblocking(fd_);
    auto a = address(path_);
    auto until = Clock::now() + std::chrono::seconds(3);
    if (::connect(fd_, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0) {
        if (errno != EINPROGRESS)
            error("connect");
        wait(fd_, POLLOUT, until);
        int status = 0;
        socklen_t n = sizeof status;
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &status, &n) < 0)
            error("connect status");
        if (status) {
            errno = status;
            error("connect status");
        }
    }
    send_all(fd_, "DASH/5\n", until);
    auto hello = Json(Object{{"type", Json("hello")},
                             {"service_id", Json(id_)},
                             {"name", Json(name_)},
                             {"instance", Json(instance_)}});
    send_all(fd_, frame(hello.serialize()), until);
    if (receive(fd_, 7, until) != "DASH/5\n")
        throw std::invalid_argument("hello rejected");
    auto ready = response(fd_, until);
    shape(ready, {"type", "receiver_instance", "canvas", "limits"});
    if (ready.get("type") != Json("ready") || ready.get("canvas") != Json(Array{Json(240.), Json(240.)}) ||
        !ready.get("limits").is<Object>())
        throw std::invalid_argument("incompatible profile");
    auto receiver = text(ready.get("receiver_instance"), 32);
    if (receiver.size() != 32 || receiver.find_first_not_of("0123456789abcdef") != receiver.npos)
        throw std::invalid_argument("invalid receiver identity");
    for (auto key : {"json_bytes", "template_bytes", "svg_bytes"}) {
        const auto &value = ready.get("limits").get(key);
        if (integer(value, 1, 9007199254740991ull) < integer(limits().get(key), 1, 9007199254740991ull))
            throw std::invalid_argument("incompatible limits");
    }
}
Json Publisher::request(const Json &j, std::string_view body) {
    auto until = Clock::now() + std::chrono::seconds(3);
    send_all(fd_, frame(j.serialize()), until);
    send_all(fd_, body, until);
    return response(fd_, until);
}
int Publisher::publish(std::string_view tiles) noexcept {
    try {
        return publish(parse(tiles));
    } catch (const std::exception &e) {
        last_error_ = e.what();
        close();
        return EPROTO;
    }
}
int Publisher::publish(const Json &data) noexcept {
    try {
        last_error_.clear();
        if (!data.is<Array>())
            throw std::invalid_argument("tiles");
        const bool changed = data != last_;
        if (changed) {
            if (revision_ == 9007199254740991ull)
                throw std::invalid_argument("revision exhausted");
            ++revision_;
            last_ = data;
            wire_ =
                Json(
                    Object{{"type", Json("publish")}, {"revision", Json(double(revision_))}, {"tiles", data}})
                    .serialize();
        }
        return exchange(changed);
    } catch (const std::system_error &e) {
        last_error_ = e.what();
        close();
        return e.code().value();
    } catch (const std::exception &e) {
        last_error_ = e.what();
        close();
        return EPROTO;
    }
}
int Publisher::heartbeat() noexcept {
    try {
        last_error_.clear();
        if (!revision_)
            throw std::invalid_argument("no snapshot");
        return exchange(false);
    } catch (const std::system_error &e) {
        last_error_ = e.what();
        close();
        return e.code().value();
    } catch (const std::exception &e) {
        last_error_ = e.what();
        close();
        return EPROTO;
    }
}
int Publisher::exchange(bool changed) {
    bool connected = fd_ >= 0;
    if (!connected)
        connect();
    if (connected && !changed) {
        auto pong = request(Json(Object{{"type", Json("ping")}, {"revision", Json(double(revision_))}}));
        if (pong.get("type") == Json("accepted") && pong.get("operation") == Json("ping") &&
            integer(pong.get("revision"), 1, 9007199254740991ull) == revision_)
            return 0;
    }
    if (connected && changed && accepted_revision_) {
        (void)request(Json(Object{{"type", Json("ping")}, {"revision", Json(double(accepted_revision_))}}));
    }
    auto until = Clock::now() + std::chrono::milliseconds(2800);
    unsigned resource_rounds = 0;
    while (Clock::now() < until) {
        auto deadline = Clock::now() + std::chrono::seconds(3);
        send_all(fd_, frame(wire_), deadline);
        auto reply = response(fd_, deadline);
        if (reply.get("type") == Json("accepted") && reply.get("operation") == Json("publish") &&
            integer(reply.get("revision"), 1, 9007199254740991ull) == revision_) {
            accepted_revision_ = revision_;
            return 0;
        }
        if (reply.get("code") != Json("missing_resources") || !reply.get("resources").is<Array>() ||
            ++resource_rounds > 3)
            throw std::invalid_argument(
                reply.get("code").is<std::string>() ? reply.get("code").get<std::string>() : "invalid_reply");
        for (const auto &item : reply.get("resources").get<Array>()) {
            auto found = resources_.find(text(item, 71));
            if (found == resources_.end())
                throw std::invalid_argument("missing producer resource");
            const auto &r = *found->second;
            auto stored = request(Json(Object{{"type", Json("put")},
                                              {"id", Json(r.id)},
                                              {"media_type", Json(r.media_type)},
                                              {"bytes", Json(double(r.bytes.size()))}}),
                                  r.bytes);
            if (stored.get("type") != Json("stored") || stored.get("id") != Json(r.id))
                throw std::invalid_argument(stored.get("code").is<std::string>()
                                                ? stored.get("code").get<std::string>()
                                                : "invalid_reply");
        }
    }
    throw std::invalid_argument("missing resource loop");
}
Server::Server(std::string path, Receiver &receiver) : path_(std::move(path)), receiver_(receiver) {
    auto a = address(path_);
    auto directory = std::filesystem::path(path_).parent_path();
    if (::mkdir(directory.c_str(), 0700) < 0 && errno != EEXIST)
        error("private socket directory");
    struct stat st{};
    if (lstat(directory.c_str(), &st) < 0)
        error("socket directory stat");
    if (!S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077))
        throw std::runtime_error("socket directory must be private");
    auto lock = directory / "lock";
    lock_ = ::open(lock.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0)
        error("socket lock");
    bool bound = false;
    try {
        if (fstat(lock_, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077))
            throw std::runtime_error("invalid socket lock");
        if (flock(lock_, LOCK_EX | LOCK_NB) < 0)
            error("socket locked");
        if (lstat(path_.c_str(), &st) == 0) {
            if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid())
                throw std::runtime_error("unexpected socket file");
            if (unlink(path_.c_str()) < 0)
                error("unlink stale socket");
        } else if (errno != ENOENT)
            error("socket stat");
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0)
            error("socket");
        nonblocking(fd_);
        if (bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0)
            error("bind");
        bound = true;
        if (chmod(path_.c_str(), 0600) < 0 || listen(fd_, 9) < 0)
            error("listen");
    } catch (...) {
        if (fd_ >= 0)
            ::close(fd_);
        if (bound)
            unlink(path_.c_str());
        ::close(lock_);
        throw;
    }
}
Server::~Server() {
    while (!clients_.empty())
        drop(clients_.size() - 1);
    if (fd_ >= 0) {
        ::close(fd_);
        unlink(path_.c_str());
    }
    if (lock_ >= 0)
        ::close(lock_);
}
void Server::drop(std::size_t i) {
    receiver_.disconnect(clients_[i].key);
    ::close(clients_[i].fd);
    clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
}
std::vector<pollfd> Server::descriptors() const {
    std::vector<pollfd> fds{{fd_, POLLIN, 0}};
    for (const auto &c : clients_)
        fds.push_back({c.fd, static_cast<short>(POLLIN | (c.sent < c.output.size() ? POLLOUT : 0)), 0});
    return fds;
}
std::optional<std::uint64_t> Server::deadline() const {
    std::optional<std::uint64_t> next;
    for (const auto &c : clients_) {
        if (c.pending && !receiver_.preparing())
            return 0;
        auto d = !c.hello    ? std::optional(c.accepted + 3000)
                 : c.started ? std::optional(*c.started + 3000)
                             : std::nullopt;
        if (c.responding && (!d || *c.responding + 3000 < *d))
            d = *c.responding + 3000;
        if (d && (!next || *d < *next))
            next = d;
    }
    return next;
}
void Server::respond(Client &client, std::uint64_t now) {
    const auto &message = *client.pending;
    const auto &json = message.decoded;
    if (json.get("type") == Json("publish") && receiver_.preparing())
        return;
    if (!client.hello) {
        auto reply = receiver_.hello(client.key, json);
        client.output = "DASH/5\n" + frame(reply.serialize());
        client.hello = reply.get("type") == Json("ready");
        client.close_after_write = !client.hello;
    } else {
        auto reply = receiver_.operation(client.key, json, message.json, message.body, now);
        if (!reply)
            return;
        client.output = std::move(*reply);
    }
    client.pending.reset();
    client.started.reset();
    client.responding = now;
}
void Server::preparation_ready(std::uint64_t now) {
    // Completion and EOF/deadline can become ready in the same poll. Inspect
    // connections first so an expired or disconnected owner cannot commit.
    pump(now);
    const auto owner = receiver_.preparing_session();
    auto client =
        std::find_if(clients_.begin(), clients_.end(), [&](const Client &c) { return c.key == owner; });
    auto reply = receiver_.finish_preparation(now);
    if (!reply || client == clients_.end())
        return;
    client->output = std::move(*reply);
    client->pending.reset();
    client->started.reset();
    client->responding = now;
}
void Server::pump(std::uint64_t now) {
    for (unsigned n = 0; n < 9; ++n) {
        int client = ::accept(fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            error("accept");
        }
        auto pending =
            std::count_if(clients_.begin(), clients_.end(), [](const Client &c) { return !c.hello; });
        if (clients_.size() >= 9 || pending >= 2 || !same_uid(client)) {
            ::close(client);
            continue;
        }
        try {
            nonblocking(client);
        } catch (...) {
            ::close(client);
            throw;
        }
        Client c;
        c.fd = client;
        c.key = std::to_string(++serial_);
        c.accepted = now;
        clients_.push_back(std::move(c));
    }
    for (std::size_t i = 0; i < clients_.size();) {
        bool remove = false;
        auto &c = clients_[i];
        try {
            if ((!c.hello && now - c.accepted >= 3000) ||
                (c.started && now >= *c.started && now - *c.started >= 3000) ||
                (c.responding && now >= *c.responding && now - *c.responding >= 3000))
                throw std::invalid_argument("deadline");
            if (c.sent < c.output.size()) {
                auto n = ::send(c.fd, c.output.data() + c.sent, c.output.size() - c.sent, MSG_NOSIGNAL);
                if (n > 0)
                    c.sent += static_cast<std::size_t>(n);
                else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    throw std::invalid_argument("send");
                if (c.sent == c.output.size()) {
                    if (c.close_after_write)
                        throw std::invalid_argument("handshake rejected");
                    c.output.clear();
                    c.sent = 0;
                    c.responding.reset();
                }
            }
            if (c.pending) {
                // A disconnected producer must not retain the preparation slot.
                char byte;
                auto n = ::recv(c.fd, &byte, 1, MSG_PEEK);
                if (n == 0)
                    throw std::invalid_argument("closed");
                if (n > 0)
                    throw std::invalid_argument("operation already pending");
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    error("peek");
                respond(c, now);
            }
            if (c.output.empty() && !c.pending) {
                std::array<char, 8192> bytes{};
                for (unsigned reads = 0; reads < 16; ++reads) {
                    auto n = ::recv(c.fd, bytes.data(), bytes.size(), 0);
                    if (n == 0)
                        throw std::invalid_argument("closed");
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR)
                            continue;
                        error("recv");
                    }
                    std::string_view input(bytes.data(), static_cast<std::size_t>(n));
                    if (!c.started)
                        c.started = now;
                    if (c.prefix.size() < 7) {
                        auto take = std::min(input.size(), 7 - c.prefix.size());
                        c.prefix += input.substr(0, take);
                        input.remove_prefix(take);
                        if (!std::string_view("DASH/5\n").starts_with(c.prefix))
                            throw std::invalid_argument("magic");
                    }
                    c.decoder.append(input);
                    Decoder::Message message;
                    if (c.prefix.size() == 7 && c.decoder.next(message)) {
                        c.pending = std::move(message);
                        respond(c, now);
                        break;
                    }
                }
            }
        } catch (const std::exception &) {
            remove = true;
        }
        if (remove)
            drop(i);
        else
            ++i;
    }
}
} // namespace kafel::dash5
