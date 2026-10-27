#include "dash5/protocol.hpp"
#include "json/document.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>
#include <type_traits>
namespace kafel::dash5 {
namespace {
[[noreturn]] void fail(const char *code) { throw std::invalid_argument(code); }
Snapshot snapshot(const Json &root) {
    shape(root, {"type", "revision", "tiles"});
    Snapshot s;
    s.revision = integer(root.get("revision"), 1, 9007199254740991ull);
    if (!root.get("tiles").is<Array>() || root.get("tiles").get<Array>().size() > tile_limit)
        fail("limit_exceeded");
    std::set<std::string> ids;
    for (const auto &j : root.get("tiles").get<Array>()) {
        shape(j, {"id", "title", "state", "attention", "freshness", "observed_at", "message", "content"});
        Tile t;
        t.id = identifier(j.get("id"));
        if (!ids.insert(t.id).second)
            fail("invalid_message");
        t.title = text(j.get("title"), 48);
        const auto state = text(j.get("state"), 7);
        if (state == "ok")
            t.state = TileState::Ok;
        else if (state == "warning")
            t.state = TileState::Warning;
        else if (state == "error")
            t.state = TileState::Error;
        else
            fail("invalid_data");
        const auto freshness = text(j.get("freshness"), 7);
        if (freshness == "fresh")
            t.freshness = Freshness::Fresh;
        else if (freshness == "stale")
            t.freshness = Freshness::Stale;
        else if (freshness == "missing")
            t.freshness = Freshness::Missing;
        else
            fail("invalid_data");
        t.message = text(j.get("message"), 96, true);
        if (!j.get("attention").is<bool>())
            fail("invalid_message");
        t.attention = j.get("attention").get<bool>();
        t.observed_at = j.get("observed_at");
        t.content = j.get("content");
        if (t.state == TileState::Error) {
            if (t.freshness != Freshness::Missing || !t.content.is<picojson::null>() ||
                !t.observed_at.is<picojson::null>() || t.message.empty())
                fail("invalid_data");
        } else {
            if (t.state != TileState::Ok && t.state != TileState::Warning)
                fail("invalid_data");
            if (t.freshness != Freshness::Fresh && t.freshness != Freshness::Stale)
                fail("invalid_data");
            if (t.state == TileState::Ok && (t.freshness != Freshness::Fresh || !t.message.empty()))
                fail("invalid_data");
            if (t.state == TileState::Warning && t.message.empty())
                fail("invalid_data");
            integer(t.observed_at, 0, 253402300799ull);
            shape(t.content, {"template", "data"});
            if (!valid_resource_id(text(t.content.get("template"), 71)))
                fail("invalid_data");
            if (!t.content.get("data").is<Object>() || t.content.get("data").get<Object>().size() > 32)
                fail("invalid_data");
            for (const auto &[k, v] : t.content.get("data").get<Object>()) {
                identifier(Json(k));
                if (v.is<std::string>())
                    text(v, 128, true);
                else if (!v.is<double>() || !std::isfinite(v.get<double>()))
                    fail("invalid_data");
            }
        }
        s.tiles.push_back(std::move(t));
    }
    return s;
}
} // namespace
bool valid_resource_id(std::string_view s) noexcept {
    return s.size() == 71 && s.starts_with("sha256:") &&
           s.substr(7).find_first_not_of("0123456789abcdef") == s.npos;
}
// SHA-256, FIPS 180-4 section 6.2. Only content addressing; never TLS/authentication.
std::string resource_id(std::string_view bytes) {
    constexpr std::array<std::uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string data(bytes);
    data += '\x80';
    while (data.size() % 64 != 56)
        data += '\0';
    auto bits = std::uint64_t(bytes.size()) * 8;
    for (int i = 7; i >= 0; --i)
        data += static_cast<char>(bits >> (8 * i));
    for (std::size_t off = 0; off < data.size(); off += 64) {
        std::array<std::uint32_t, 64> w{};
        for (unsigned i = 0; i < 16; ++i)
            for (unsigned j = 0; j < 4; ++j)
                w[i] = (w[i] << 8) | static_cast<unsigned char>(data[off + 4 * i + j]);
        for (unsigned i = 16; i < 64; ++i) {
            auto a = w[i - 15], b = w[i - 2];
            w[i] = w[i - 16] + (std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
                   (std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10));
        }
        auto [a, b, c, d, e, f, g, v] = h;
        for (unsigned i = 0; i < 64; ++i) {
            auto t = v + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                     k[i] + w[i];
            auto u = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            v = g;
            g = f;
            f = e;
            e = d + t;
            d = c;
            c = b;
            b = a;
            a = t + u;
        }
        std::array x = {a, b, c, d, e, f, g, v};
        for (unsigned i = 0; i < 8; ++i)
            h[i] += x[i];
    }
    std::string result = "sha256:";
    char buf[9];
    for (auto x : h) {
        std::snprintf(buf, sizeof buf, "%08x", x);
        result += buf;
    }
    return result;
}
std::string random_instance() {
    std::array<unsigned char, 16> b{};
    std::ifstream source("/dev/urandom", std::ios::binary);
    if (!source.read(reinterpret_cast<char *>(b.data()), b.size()))
        throw std::runtime_error("random instance");
    std::string out;
    for (auto c : b) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string frame(std::string_view s) {
    if (s.empty() || s.size() > frame_limit)
        fail("limit_exceeded");
    std::string out;
    for (int i = 3; i >= 0; --i)
        out += static_cast<char>(s.size() >> (i * 8));
    out += s;
    return out;
}
void Decoder::append(std::string_view s) {
    if (buffer_.size() + s.size() > 2 * frame_limit + 4)
        fail("limit_exceeded");
    buffer_ += s;
}
bool Decoder::next(Message &out) {
    if (header_.empty()) {
        if (buffer_.size() < 4)
            return false;
        std::size_t n = 0;
        for (unsigned i = 0; i < 4; ++i)
            n = (n << 8) | static_cast<unsigned char>(buffer_[i]);
        if (n < 1 || n > frame_limit)
            fail("limit_exceeded");
        if (buffer_.size() < n + 4)
            return false;
        header_ = buffer_.substr(4, n);
        buffer_.erase(0, n + 4);
        decoded_ = parse(header_);
        const auto &j = decoded_;
        body_bytes_ = 0;
        if (j.is<Object>() && j.get("type") == Json("put"))
            body_bytes_ = integer(j.get("bytes"), 1, 65536);
    }
    if (buffer_.size() < body_bytes_)
        return false;
    out = {std::move(header_), buffer_.substr(0, body_bytes_), std::move(decoded_)};
    buffer_.erase(0, body_bytes_);
    header_.clear();
    body_bytes_ = 0;
    return true;
}
Json rejected(std::string_view op, std::string_view code) {
    return Json(Object{
        {"type", Json("rejected")}, {"operation", Json(std::string(op))}, {"code", Json(std::string(code))}});
}
Json limits() {
    return parse(
        R"PROFILE({"services":7,"tiles_total":13,"tiles_per_service":13,"json_bytes":65536,"json_depth":{"transport":16,"template":32},"template_bytes":32768,"svg_bytes":65536,"resources_per_service":64,"resource_bytes_per_service":1048576,"resource_bytes_total":4194304,"fields_per_tile":32,"text_bytes":128,"fonts_per_template":4,"glyphs_per_font":512,"styles_per_template":16,"font_sizes_per_template":8,"elements_per_template":64,"images_per_template":4,"svg_depth":32,"svg_shapes":1024,"svg_points":65536,"svg_intrinsic_extent":4096,"glyph_cache_bytes":8388608,"candidate_bytes":8388608,"preparing_snapshots":1,"pending_connections":2,"operation_timeout_ms":3000,"prepare_timeout_ms":2000,"heartbeat_ms":5000,"offline_ms":15000,"animation_fps":25,"rgba_scratch_bytes":460800,"rgb_card_cache_bytes":2246400,"corner_cache_bytes":187200})PROFILE");
}
namespace {
std::vector<std::string> resource_fields(const Resource &resource) {
    if (resource.media_type != "application/vnd.kafel.tile+json")
        return {};
    const auto definition = parse(resource.bytes, 32);
    if (!definition.is<Object>() || !definition.get("fields").is<Array>() ||
        definition.get("fields").get<Array>().size() > 32)
        fail("invalid_template");
    std::vector<std::string> names;
    for (const auto &field : definition.get("fields").get<Array>())
        if (field.is<Object>() && field.get("format") == Json("svg"))
            names.push_back(identifier(field.get("name")));
    return names;
}
std::string reply_bytes(const Json &reply) { return frame(reply.serialize()); }
std::string accepted(std::string_view operation, std::uint64_t revision) {
    return reply_bytes(Json(Object{{"type", Json("accepted")},
                                   {"operation", Json(std::string(operation))},
                                   {"revision", Json(double(revision))}}));
}
} // namespace
Json Receiver::hello(std::string_view key, const Json &j) {
    shape(j, {"type", "service_id", "name", "instance"});
    if (j.get("type") != Json("hello"))
        fail("invalid_message");
    auto id = identifier(j.get("service_id")), name = text(j.get("name"), 48),
         instance = text(j.get("instance"), 32);
    if (instance.size() != 32 || instance.find_first_not_of("0123456789abcdef") != instance.npos)
        fail("invalid_message");
    for (const auto &[k, s] : sessions_)
        if (k != key && s.id == id)
            return rejected("hello", "service_busy");
    if (sessions_.size() >= service_limit)
        return rejected("hello", "limit_exceeded");
    sessions_.emplace(std::string(key), Session{id, name, instance, {}});
    return Json(Object{{"type", Json("ready")},
                       {"receiver_instance", Json(instance_)},
                       {"canvas", Json(Array{Json(240.), Json(240.)})},
                       {"limits", limits()}});
}
void Receiver::collect() {
    for (auto it = resources_.begin(); it != resources_.end();)
        if (it->second.use_count() == 1)
            it = resources_.erase(it);
        else
            ++it;
}
void Receiver::disconnect(std::string_view key) {
    if (pending_ && pending_->session == key)
        cancel_preparation();
    sessions_.erase(std::string(key));
    collect();
}
std::optional<std::string> Receiver::operation(std::string_view key, std::string_view wire,
                                               std::string_view body, std::uint64_t now) {
    try {
        return operation(key, parse(wire), wire, body, now);
    } catch (const std::invalid_argument &) {
        return reply_bytes(rejected("", "invalid_message"));
    }
}
std::optional<std::string> Receiver::operation(std::string_view key, const Json &j, std::string_view wire,
                                               std::string_view body, std::uint64_t now) {
    std::string op;
    std::uint64_t rev = 0;
    try {
        auto session = sessions_.find(key);
        if (session == sessions_.end())
            fail("invalid_message");
        auto &s = session->second;
        if (!j.is<Object>())
            fail("invalid_message");
        op = text(j.get("type"), 16);
        auto entry =
            std::find_if(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.id == s.id; });
        if (op == "put") {
            shape(j, {"type", "id", "media_type", "bytes"});
            auto id = text(j.get("id"), 71), mime = text(j.get("media_type"), 40);
            if (!valid_resource_id(id) ||
                (mime != "image/svg+xml" && mime != "application/vnd.kafel.tile+json"))
                fail("invalid_message");
            auto n = integer(j.get("bytes"), 1, mime == "image/svg+xml" ? 65536 : 32768);
            if (n != body.size() || resource_id(body) != id)
                fail("hash_mismatch");
            Resource incoming{id, mime, std::string(body)};
            incoming.svg_fields = validate_ ? validate_(incoming) : resource_fields(incoming);
            Resources owned = s.staged;
            if (entry != entries_.end())
                for (const auto &[k, r] : entry->pinned)
                    owned[k] = r;
            std::size_t owned_bytes = 0;
            for (const auto &[k, r] : owned) {
                (void)k;
                owned_bytes += r->bytes.size();
            }
            if (!owned.contains(id) && (owned.size() >= 64 || owned_bytes + n > 1048576))
                fail("limit_exceeded");
            collect();
            if (auto old = resources_.find(id); old != resources_.end()) {
                if (old->second->media_type != mime)
                    fail("resource_conflict");
                s.staged[id] = old->second;
            } else {
                std::size_t total = 0, owned = 0;
                for (const auto &[k, r] : resources_) {
                    (void)k;
                    total += r->bytes.size();
                }
                for (const auto &[k, r] : s.staged) {
                    (void)k;
                    owned += r->bytes.size();
                }
                if (s.staged.size() >= 64 || owned + n > 1048576 || total + n > 4194304)
                    fail("limit_exceeded");
                auto r = std::make_shared<Resource>(std::move(incoming));
                resources_[id] = r;
                s.staged[id] = std::move(r);
            }
            return reply_bytes(Json(Object{{"type", Json("stored")}, {"id", Json(id)}}));
        }
        if (op == "ping") {
            shape(j, {"type", "revision"});
            rev = integer(j.get("revision"), 1, 9007199254740991ull);
            if (entry == entries_.end() || entry->instance != s.instance || entry->snapshot.revision != rev)
                fail("no_snapshot");
            auto reply = accepted(op, rev);
            if (entry->offline) {
                entry->offline = false;
                ++generation_;
            }
            entry->seen = now;
            return reply;
        } else if (op == "publish") {
            auto candidate = snapshot(j);
            rev = candidate.revision;
            if (entry != entries_.end() && entry->instance == s.instance) {
                if (rev < entry->snapshot.revision)
                    fail("stale_revision");
                if (rev == entry->snapshot.revision) {
                    if (wire != entry->wire)
                        fail("revision_conflict");
                    auto reply = accepted(op, rev);
                    if (entry->offline) {
                        entry->offline = false;
                        ++generation_;
                    }
                    entry->seen = now;
                    return reply;
                }
            }
            std::size_t total = candidate.tiles.size();
            for (const auto &e : entries_)
                if (e.id != s.id)
                    total += e.snapshot.tiles.size();
            if (total > tile_limit || (entry == entries_.end() && entries_.size() >= service_limit))
                fail("limit_exceeded");
            Array missing;
            Resources pinned;
            auto need = [&](const std::string &id, const char *mime) {
                auto found = resources_.find(id);
                if (found == resources_.end()) {
                    if (std::find(missing.begin(), missing.end(), Json(id)) == missing.end())
                        missing.emplace_back(id);
                } else {
                    if (found->second->media_type != mime)
                        fail("resource_conflict");
                    pinned[id] = found->second;
                }
            };
            for (const auto &t : candidate.tiles)
                if (!t.content.is<picojson::null>()) {
                    auto id = t.content.get("template").get<std::string>();
                    need(id, "application/vnd.kafel.tile+json");
                    if (auto found = resources_.find(id); found != resources_.end()) {
                        for (const auto &name : found->second->svg_fields) {
                            const auto &data = t.content.get("data");
                            if (!data.contains(name))
                                fail("invalid_data");
                            auto resource = text(data.get(name), 71);
                            if (!valid_resource_id(resource))
                                fail("invalid_data");
                            need(resource, "image/svg+xml");
                        }
                    }
                }
            if (missing.size() + pinned.size() > 64)
                fail("limit_exceeded");
            if (!missing.empty()) {
                auto error = rejected(op, "missing_resources");
                error.get<Object>()["resources"] = Json(missing);
                error.get<Object>()["revision"] = Json(double(rev));
                return reply_bytes(error);
            }
            // Dependencies may already be cached by another service. Admission must
            // still charge their bytes to this service, together with its current
            // active and staged resources; each content-addressed ID counts once.
            Resources owned = s.staged;
            if (entry != entries_.end())
                for (const auto &[id, resource] : entry->pinned)
                    owned[id] = resource;
            for (const auto &[id, resource] : pinned)
                owned[id] = resource;
            std::size_t owned_bytes = 0;
            for (const auto &[id, resource] : owned) {
                (void)id;
                owned_bytes += resource->bytes.size();
            }
            if (owned.size() > 64 || owned_bytes > 1048576)
                fail("limit_exceeded");
            if (pending_)
                fail("service_busy");
            Entry next{.id = s.id,
                       .name = s.name,
                       .instance = s.instance,
                       .wire = std::string(wire),
                       .snapshot = std::move(candidate),
                       .presentation = {},
                       .seen = now,
                       .offline = false,
                       .pinned = std::move(pinned)};
            // Establish ownership before starting the physical worker. If
            // bookkeeping allocation fails, no orphan task has been launched.
            pending_ = PendingPublication{std::string(key), std::move(next)};
            std::optional<Prepared> prepared;
            try {
                prepared = prepare_(pending_->entry.snapshot, pending_->entry.pinned);
            } catch (...) {
                pending_.reset();
                throw;
            }
            if (!prepared) {
                if (!finish_)
                    throw std::logic_error("missing preparation completion callback");
                return std::nullopt;
            }
            auto publication = std::move(*pending_);
            pending_.reset();
            publication.entry.presentation = std::move(*prepared);
            return commit(key, std::move(publication.entry), now);
        } else
            fail("invalid_message");
        return reply_bytes(Json(
            Object{{"type", Json("accepted")}, {"operation", Json(op)}, {"revision", Json(double(rev))}}));
    } catch (const std::invalid_argument &e) {
        const std::string_view code = e.what();
        constexpr std::array codes = {"invalid_message",   "unsupported_schema", "invalid_template",
                                      "invalid_svg",       "invalid_data",       "hash_mismatch",
                                      "resource_conflict", "limit_exceeded",     "stale_revision",
                                      "revision_conflict", "no_snapshot",        "service_busy",
                                      "prepare_timeout"};
        auto error = rejected(op, std::find(codes.begin(), codes.end(), code) != codes.end()
                                      ? code
                                      : std::string_view("invalid_data"));
        if (rev)
            error.get<Object>()["revision"] = Json(double(rev));
        return reply_bytes(error);
    } catch (const std::bad_alloc &) {
        return reply_bytes(rejected(op, "limit_exceeded"));
    } catch (const std::logic_error &) {
        throw; // Broken application contracts are not malformed service data.
    } catch (const std::exception &) {
        return reply_bytes(rejected(op, "invalid_message"));
    }
}
std::string Receiver::commit(std::string_view key, Entry next, std::uint64_t now) {
    auto session = sessions_.find(key);
    if (session == sessions_.end())
        return reply_bytes(rejected("publish", "invalid_message"));
    auto entry =
        std::find_if(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.id == next.id; });
    if (!next.presentation || next.presentation->frames.size() != next.snapshot.tiles.size())
        throw std::invalid_argument("invalid_data");
    for (std::size_t i = 0; i < next.snapshot.tiles.size(); ++i)
        if (bool(next.presentation->frames[i]) != (next.snapshot.tiles[i].state != TileState::Error))
            throw std::invalid_argument("invalid_data");
    const bool inserting = entry == entries_.end();
    const bool registered = inserting || entry->instance != next.instance;
    // Both reply serialization and vector growth can fail. Finish them before
    // changing the old snapshot, TTL, staged resources or generation.
    auto reply = accepted("publish", next.snapshot.revision);
    if (inserting)
        entries_.reserve(entries_.size() + 1);
    next.seen = now;
    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    static_assert(std::is_nothrow_move_assignable_v<Entry>);
    if (inserting)
        entries_.push_back(std::move(next));
    else
        *entry = std::move(next);
    session->second.staged.clear();
    collect();
    ++generation_;
    if (registered)
        std::fprintf(stderr, "services: registered %s (DASH/5)\n", session->second.id.c_str());
    return reply;
}
void Receiver::cancel_preparation() {
    // Keep ownership until the worker is drained. No second queue or duplicate
    // cancellation state is needed in the worker or Unix transport.
    if (pending_)
        pending_->cancelled = true;
}
std::optional<std::string> Receiver::finish_preparation(std::uint64_t now) {
    if (!pending_)
        throw std::logic_error("no pending publication");
    const auto revision = pending_->entry.snapshot.revision;
    const auto cancelled = pending_->cancelled;
    try {
        auto prepared = finish_();
        auto publication = std::move(*pending_);
        pending_.reset();
        if (cancelled)
            return std::nullopt;
        publication.entry.presentation = std::move(prepared);
        return commit(publication.session, std::move(publication.entry), now);
    } catch (const std::bad_alloc &) {
        pending_.reset();
        if (cancelled)
            return std::nullopt;
        auto reply = rejected("publish", "limit_exceeded");
        reply.get<Object>()["revision"] = Json(double(revision));
        return reply_bytes(reply);
    } catch (const std::invalid_argument &error) {
        pending_.reset();
        if (cancelled)
            return std::nullopt;
        constexpr std::array allowed = {"invalid_template", "invalid_svg", "invalid_data", "limit_exceeded",
                                        "prepare_timeout"};
        const std::string_view reason = error.what();
        const auto code = std::find(allowed.begin(), allowed.end(), reason) != allowed.end()
                              ? reason
                              : std::string_view("invalid_data");
        auto reply = rejected("publish", code);
        reply.get<Object>()["revision"] = Json(double(revision));
        return reply_bytes(reply);
    }
}
bool Receiver::expire(std::uint64_t now) {
    bool changed = false;
    for (auto &e : entries_)
        if (!e.offline && now >= e.seen && now - e.seen >= 15000) {
            e.offline = true;
            changed = true;
        }
    if (changed)
        ++generation_;
    return changed;
}
} // namespace kafel::dash5
