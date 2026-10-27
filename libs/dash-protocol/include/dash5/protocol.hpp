#pragma once
#include "prepared.hpp"
#include "state.hpp"
#include "json/strict.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace kafel::dash5 {
using Json = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
inline constexpr std::size_t frame_limit = 65536, tile_limit = 13, service_limit = 7;
using json::identifier;
using json::integer;
using json::parse;
using json::shape;
using json::text;
std::string resource_id(std::string_view);
bool valid_resource_id(std::string_view) noexcept;
std::string random_instance();
std::string frame(std::string_view);
struct Resource {
    std::string id, media_type, bytes;
    std::vector<std::string> svg_fields{};
};
using Resources = std::map<std::string, std::shared_ptr<const Resource>, std::less<>>;
struct Tile {
    std::string id, title, message;
    TileState state = TileState::Ok;
    Freshness freshness = Freshness::Fresh;
    bool attention = false;
    Json observed_at, content;
};
struct Snapshot {
    std::uint64_t revision = 0;
    std::vector<Tile> tiles;
};
struct Entry {
    std::string id, name, instance, wire;
    Snapshot snapshot;
    std::shared_ptr<const PreparedTiles> presentation;
    std::uint64_t seen = 0;
    bool offline = false;
    Resources pinned;
};
// Validation returns the already decoded SVG binding names. The receiver can
// account for dependencies without parsing the same template a second time.
using Validate = std::function<std::vector<std::string>(const Resource &)>;
// A missing result means preparation is pending, never a protocol rejection.
using Prepared = std::shared_ptr<const PreparedTiles>;
using Prepare = std::function<std::optional<Prepared>(const Snapshot &, const Resources &)>;
using FinishPreparation = std::function<Prepared()>;
class Receiver {
  public:
    explicit Receiver(Prepare prepare, Validate validate = {}, FinishPreparation finish = {})
        : prepare_(std::move(prepare)), validate_(std::move(validate)), finish_(std::move(finish)),
          instance_(random_instance()) {}
    Json hello(std::string_view session, const Json &);
    // Ready results are fully framed replies, built before the model is committed.
    // Nullopt means a running preparation; it is never sent to a producer.
    std::optional<std::string> operation(std::string_view session, std::string_view json,
                                         std::string_view body, std::uint64_t now);
    // Decoded by the transport once; wire is retained only for revision identity.
    std::optional<std::string> operation(std::string_view session, const Json &, std::string_view wire,
                                         std::string_view body, std::uint64_t now);
    // Called after worker completion. A cancelled candidate returns no reply.
    std::optional<std::string> finish_preparation(std::uint64_t now);
    void cancel_preparation();
    bool preparing() const noexcept { return pending_.has_value(); }
    std::string_view preparing_session() const noexcept {
        return pending_ ? std::string_view(pending_->session) : std::string_view{};
    }
    void disconnect(std::string_view session);
    bool expire(std::uint64_t now);
    const auto &entries() const noexcept { return entries_; }
    std::uint64_t generation() const noexcept { return generation_; }

  private:
    struct Session {
        std::string id, name, instance;
        Resources staged;
    };
    std::map<std::string, Session, std::less<>> sessions_;
    std::vector<Entry> entries_;
    Resources resources_;
    Prepare prepare_;
    Validate validate_;
    FinishPreparation finish_;
    struct PendingPublication {
        std::string session;
        Entry entry;
        bool cancelled = false;
    };
    std::optional<PendingPublication> pending_;
    std::string instance_;
    std::uint64_t generation_ = 0;
    void collect();
    std::string commit(std::string_view session, Entry next, std::uint64_t now);
};
// Incremental framing is shared by the real server and transport tests.
class Decoder {
  public:
    struct Message {
        std::string json, body;
        Json decoded;
    };
    void append(std::string_view);
    bool next(Message &);
    bool partial() const noexcept { return !buffer_.empty() || !header_.empty(); }

  private:
    std::string buffer_, header_;
    Json decoded_;
    std::size_t body_bytes_ = 0;
};
Json limits();
Json rejected(std::string_view operation, std::string_view code);
} // namespace kafel::dash5
