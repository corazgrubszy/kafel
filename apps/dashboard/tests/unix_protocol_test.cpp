#include "dash5/unix.hpp"
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
void check(bool, const char *);
namespace {
using namespace kafel::dash5;
int peer(const std::string &path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    check(fd >= 0, "create test peer");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    check(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0, "connect test peer");
    timeval timeout{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    return fd;
}
void write(int fd, std::string_view text) {
    while (!text.empty()) {
        auto n = ::send(fd, text.data(), text.size(), 0);
        check(n > 0, "write test peer");
        text.remove_prefix(static_cast<std::size_t>(n));
    }
}
std::string read(int fd, std::size_t count) {
    std::string result(count, '\0');
    for (std::size_t offset = 0; offset < count;) {
        auto n = recv(fd, result.data() + offset, count - offset, 0);
        check(n > 0, "read complete server response");
        offset += static_cast<std::size_t>(n);
    }
    return result;
}
Json reply(int fd) {
    auto length = read(fd, 4);
    std::size_t n = 0;
    for (auto b : length)
        n = n * 256 + static_cast<unsigned char>(b);
    check(n < 65537, "bounded response");
    return parse(read(fd, n));
}
std::string hello() {
    return R"({"type":"hello","service_id":"test","name":"Test","instance":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
}
} // namespace
void test_unix_protocol() {
    using namespace kafel::dash5;
    char directory[] = "/tmp/kafel-wire.XXXXXX";
    check(mkdtemp(directory) != nullptr, "private wire directory");
    auto path = std::string(directory) + "/dashboard.sock";
    Receiver receiver(
        [](const Snapshot &, const Resources &) { return std::make_shared<const PreparedTiles>(); });
    {
        Server server(path, receiver);
        struct stat st{};
        check(lstat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "socket permission 0600");
        bool refused = false;
        try {
            Server duplicate(path, receiver);
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "second server cannot replace live socket");
        int slow = peer(path);
        write(slow, "D");
        server.pump(0);
        check(server.deadline() == 3000, "partial prefix deadline starts at monotonic zero");
        server.pump(2999);
        check(server.descriptors().size() == 2, "slow prefix retained before exact deadline");
        server.pump(3000);
        char byte;
        check(recv(slow, &byte, 1, 0) == 0, "slow prefix closes at exact deadline");
        close(slow);
        int fd = peer(path);
        write(fd, "DASH/5\n" + frame(hello()));
        server.pump(4000);
        server.pump(4001);
        check(read(fd, 7) == "DASH/5\n" && reply(fd).get("type") == Json("ready"), "real handshake");
        int duplicate = peer(path);
        write(duplicate, "DASH/5\n" + frame(hello()));
        server.pump(4100);
        server.pump(4101);
        check(read(duplicate, 7) == "DASH/5\n" && reply(duplicate).get("code") == Json("service_busy"),
              "duplicate producer gets framed rejection");
        check(recv(duplicate, &byte, 1, 0) == 0, "rejected handshake closes after flushing reply");
        close(duplicate);
#ifdef KAFEL_JSON_TEST_STATS
        const auto parses_before = kafel::json::parsed_documents;
#endif
        write(fd, frame(R"({"type":"publish","revision":1,"tiles":[]})"));
        server.pump(4200);
        server.pump(4201);
#ifdef KAFEL_JSON_TEST_STATS
        check(kafel::json::parsed_documents == parses_before + 1,
              "one publication is decoded once across framing, server and receiver");
#endif
        check(reply(fd).get("type") == Json("accepted"), "empty authoritative snapshot");
        auto put = Json(Object{{"type", Json("put")},
                               {"id", Json(resource_id(std::string(100, 'x')))},
                               {"media_type", Json("image/svg+xml")},
                               {"bytes", Json(100.)}})
                       .serialize();
        write(fd, frame(put) + "xxx");
        server.pump(4300);
        check(server.deadline() == 7300, "resource body has absolute deadline");
        server.pump(7300);
        check(recv(fd, &byte, 1, 0) == 0, "incomplete body discarded");
        close(fd);
        check(receiver.entries()[0].snapshot.revision == 1 && receiver.entries()[0].seen == 4200,
              "partial upload never alters snapshot or heartbeat");
        int old = peer(path);
        write(old, "DASH/4\n");
        server.pump(8000);
        check(recv(old, &byte, 1, 0) == 0, "old wire magic rejected without negotiation");
        close(old);
    }
    {
        bool ready = false;
        unsigned preparations = 0, completions = 0;
        Receiver pending_receiver(
            [&](const Snapshot &, const Resources &) -> std::optional<Prepared> {
                ++preparations;
                if (!ready)
                    return std::nullopt;
                return std::make_shared<const PreparedTiles>();
            },
            {},
            [&]() -> Prepared {
                ++completions;
                return std::make_shared<const PreparedTiles>();
            });
        Server server(path, pending_receiver);
        auto handshake = [&](const char *id, std::uint64_t now) {
            int fd = peer(path);
            auto h = parse(hello());
            h.get<Object>()["service_id"] = Json(std::string(id));
            write(fd, "DASH/5\n" + frame(h.serialize()));
            server.pump(now);
            server.pump(now + 1);
            check(read(fd, 7) == "DASH/5\n" && reply(fd).get("type") == Json("ready"),
                  "pending peer handshake");
            return fd;
        };
        int waiter = handshake("waiter", 0), owner = handshake("owner", 10);
        const auto packet = frame(R"({"type":"publish","revision":1,"tiles":[]})");
        write(owner, packet);
        server.pump(20);
        char byte;
        check(recv(owner, &byte, 1, MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
              "pending preparation sends no busy response and requests no retransmission");
        check(preparations == 1, "one wire publication starts one preparation");
        write(waiter, packet);
        server.pump(21);
        check(preparations == 1, "I/O from other peers does not poll or rebuild pending preparation");
        close(owner);
        server.pump(22);
        check(pending_receiver.preparing() && preparations == 1,
              "cancelled physical job stays owned until completion; no second worker starts");
        ready = true;
        server.preparation_ready(23);
        check(completions == 1 && !pending_receiver.preparing(), "cancelled worker drained once");
        server.pump(24);
        server.pump(25);
        check(reply(waiter).get("type") == Json("accepted") && preparations == 2,
              "waiting service publishes without orphan reservation or retransmission");
        check(pending_receiver.entries().size() == 1 && pending_receiver.entries()[0].id == "waiter",
              "abandoned candidate never becomes visible");
        ready = false;
        write(waiter, frame(R"({"type":"publish","revision":2,"tiles":[]})"));
        server.pump(25);
        server.pump(26);
        check(preparations == 3, "pending publication is not polled without completion notification");
        ready = true;
#ifdef KAFEL_JSON_TEST_STATS
        const auto parses_before_resume = kafel::json::parsed_documents;
#endif
        server.preparation_ready(27);
        server.pump(27);
        server.pump(28);
#ifdef KAFEL_JSON_TEST_STATS
        check(kafel::json::parsed_documents == parses_before_resume,
              "completion resumes a decoded publication without parsing JSON again");
#endif
        check(reply(waiter).get("type") == Json("accepted") && preparations == 3,
              "completion commits the candidate without rerunning preparation");
        close(waiter);
        server.pump(30);
        ready = false;
        int expired = handshake("deadline", 1000);
        write(expired, packet);
        server.pump(1002);
        ready = true;
        server.preparation_ready(4002);
        check(pending_receiver.entries().size() == 1 && !pending_receiver.preparing(),
              "completion at deadline drains worker without committing expired publication");
        check(recv(expired, &byte, 1, 0) == 0, "expired peer closed before completion reply");
        close(expired);
        ready = false;
        int vanished = handshake("eof", 5000);
        write(vanished, packet);
        server.pump(5002);
        close(vanished);
        ready = true;
        server.preparation_ready(5003);
        check(pending_receiver.entries().size() == 1 && !pending_receiver.preparing(),
              "EOF simultaneous with completion is observed before commit");
    }
    // A stale path is removable only when it is an owned socket, never a symlink.
    check(symlink("/tmp/kafel-no-such-target", path.c_str()) == 0, "symlink fixture");
    bool refused = false;
    try {
        Server invalid(path, receiver);
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, "symlink socket refused");
    struct stat st{};
    check(lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode), "refusal leaves unrelated path intact");
    unlink(path.c_str());
    unlink((std::string(directory) + "/lock").c_str());
    rmdir(directory);
    std::puts("PASS Unix stream privacy, live lock, handshake NACK, prefix/body deadlines, EOF, old-version "
              "rejection and symlink safety");
}
