#pragma once
#include "protocol.hpp"
#include <optional>
#include <poll.h>

namespace kafel::dash5 {
std::string default_socket_path();
bool valid_socket_path(std::string_view) noexcept;
class Publisher {
  public:
    Publisher(std::string path, std::string id, std::string name, Resources resources);
    ~Publisher();
    Publisher(const Publisher &) = delete;
    Publisher &operator=(const Publisher &) = delete;
    int publish(std::string_view tiles_json) noexcept;
    int publish(const Json &tiles) noexcept;
    int heartbeat() noexcept;

    const std::string &error_message() const noexcept { return last_error_; }

  private:
    std::string path_, id_, name_, instance_, wire_, last_error_;
    Json last_;
    int exchange(bool changed);
    Resources resources_;
    std::uint64_t revision_ = 0, accepted_revision_ = 0;
    int fd_ = -1;
    void connect();
    Json request(const Json &, std::string_view body = {});
    void close() noexcept;
};
class Server {
  public:
    Server(std::string path, Receiver &receiver);
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    std::vector<pollfd> descriptors() const;
    void pump(std::uint64_t now);
    void preparation_ready(std::uint64_t now);
    std::optional<std::uint64_t> deadline() const;
    const std::string &path() const { return path_; }

  private:
    struct Client {
        int fd = -1;
        std::string key, prefix, output;
        std::size_t sent = 0;
        Decoder decoder;
        std::optional<Decoder::Message> pending;
        bool hello = false, close_after_write = false;
        std::uint64_t accepted = 0;
        std::optional<std::uint64_t> started, responding;
    };
    int fd_ = -1, lock_ = -1;
    std::string path_;
    Receiver &receiver_;
    std::vector<Client> clients_;
    std::uint64_t serial_ = 0;
    void respond(Client &, std::uint64_t);
    void drop(std::size_t);
};
} // namespace kafel::dash5
