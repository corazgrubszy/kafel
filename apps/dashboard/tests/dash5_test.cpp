#include "dash5/protocol.hpp"
#include "ui/card.hpp"
#include <array>
#include <cstdio>
#include <fstream>
void check(bool, const char *);
void fail_allocation_after(std::size_t);
bool stop_allocation_failure();
namespace {
using namespace kafel::dash5;
template <class F> void rejects(F action, std::string_view expected = "invalid_message") {
    bool failed = false;
    try {
        action();
    } catch (const std::invalid_argument &error) {
        check(error.what() == expected, "expected rejection code");
        failed = true;
    }
    check(failed, "malformed input rejected");
}
Json decoded_reply(std::string_view wire) {
    check(wire.size() > 4, "framed receiver reply");
    return parse(wire.substr(4));
}
Prepared prepared_tiles(const Snapshot &snapshot) {
    static const auto image =
        std::make_shared<const ui::CardFrame>(std::vector<unsigned char>(240 * 240 * 3));
    std::vector<std::shared_ptr<const ui::CardFrame>> frames;
    for (const auto &tile : snapshot.tiles)
        frames.push_back(tile.state == TileState::Error ? nullptr : image);
    return std::make_shared<const PreparedTiles>(std::move(frames));
}
Json sync_operation(Receiver &receiver, std::string_view session, std::string_view wire,
                    std::string_view body, std::uint64_t now) {
    auto reply = receiver.operation(session, wire, body, now);
    check(reply.has_value(), "synchronous preparation returns a reply");
    return decoded_reply(*reply);
}
Json hello(const char *instance = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") {
    return Json(Object{{"type", Json("hello")},
                       {"service_id", Json("test")},
                       {"name", Json("Test")},
                       {"instance", Json(instance)}});
}
Json missing_tile() {
    return Json(Object{{"id", Json("one")},
                       {"title", Json("Test")},
                       {"state", Json("error")},
                       {"attention", Json(false)},
                       {"freshness", Json("missing")},
                       {"observed_at", Json()},
                       {"message", Json("Brak danych")},
                       {"content", Json()}});
}
Json publish(unsigned revision, const Json &tile) {
    return Json(Object{
        {"type", Json("publish")}, {"revision", Json(double(revision))}, {"tiles", Json(Array{tile})}});
}
std::string file(const char *name) {
    std::ifstream f(name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
} // namespace
void test_dash5_framing() {
    using namespace kafel::dash5;
    check(resource_id("") == "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA256 empty");
    check(resource_id("abc") == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA256 abc");
    check(resource_id(std::string(1000000, 'a')) ==
              "sha256:cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA256 multi-block");
    for (auto bad :
         {"{\"x\":1,\"x\":2}", "{\"x\":1,\"\\u0078\":2}", "{}{}", "{\"x\":1e999}", "{\"x\":\"\xc0\x80\"}"})
        rejects([&] { parse(bad); });
    rejects([] { parse(std::string(17, '[') + "0" + std::string(17, ']')); });
    rejects([] { text(Json("a\nb"), 32); });
    rejects([] { frame({}); }, "limit_exceeded");
    rejects([] { frame(std::string(65537, 'x')); }, "limit_exceeded");
    const auto body = std::string("<svg width='8' height='8'></svg>");
    const auto id = resource_id(body);
    auto put = Json(Object{{"type", Json("put")},
                           {"id", Json(id)},
                           {"media_type", Json("image/svg+xml")},
                           {"bytes", Json(double(body.size()))}})
                   .serialize();
    const auto wire = frame(put) + body;
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        Decoder decoder;
        Decoder::Message message;
        decoder.append(std::string_view(wire).substr(0, split));
        bool done = decoder.next(message);
        decoder.append(std::string_view(wire).substr(split));
        if (!done)
            check(decoder.next(message), "split frame completes");
        check(message.json == put && message.body == body && message.decoded == parse(put),
              "every framing split preserves bytes and decoded document");
        check(!decoder.next(message), "no extra frame");
    }
    Decoder multiple;
    multiple.append(frame("{}") + frame("[]"));
    Decoder::Message message;
    check(multiple.next(message) && message.json == "{}" && multiple.next(message) && message.json == "[]",
          "coalesced frames");
    rejects(
        [] {
            Decoder d;
            d.append(std::string(4, '\0'));
            Decoder::Message m;
            d.next(m);
        },
        "limit_exceeded");
    for (auto bad : {"{\"x\":1,\"x\":2}", "{\"nested\":{\"x\":1,\"x\":2}}",
                     "{\"array\":[{\"x\":1,\"\\u0078\":2}]}", "{\"x\":\"\xc0\xaf\"}"})
        check(!kafel::json::document(bad),
              "shared DOM rejects nested/escaped duplicates and malformed Unicode directly");
    check(kafel::json::document(std::string(16, '[') + "0" + std::string(16, ']'), 65536, 16).has_value(),
          "exact shared parser depth");
    check(!kafel::json::document(std::string(17, '[') + "0" + std::string(17, ']'), 65536, 16),
          "shared parser rejects excess depth during descent");
    std::puts("PASS DASH/5 framing and exact validation codes");
}
void test_dash5_receiver() {
    using namespace kafel::dash5;
    const std::string body = "<svg width='8' height='8'></svg>";
    const auto put = Json(Object{{"type", Json("put")},
                                 {"id", Json(resource_id(body))},
                                 {"media_type", Json("image/svg+xml")},
                                 {"bytes", Json(double(body.size()))}})
                         .serialize();
    unsigned prepares = 0;
    bool reject_prepare = false;
    Receiver receiver([&](const Snapshot &snapshot, const Resources &) -> Prepared {
        ++prepares;
        if (reject_prepare)
            throw std::invalid_argument("invalid_template");
        return prepared_tiles(snapshot);
    });
    check(receiver.hello("one", hello()).get("type") == Json("ready"), "hello");
    check(receiver.hello("two", hello()).get("code") == Json("service_busy"), "duplicate publisher refused");
    auto initial = publish(1, missing_tile()).serialize();
    auto accepted = sync_operation(receiver, "one", initial, {}, 100);
    check(accepted.get("type") == Json("accepted") && receiver.entries().size() == 1,
          "first complete publish");
    check(prepares == 1, "prepared once");
    check(sync_operation(receiver, "one", initial, {}, 200).get("type") == Json("accepted") && prepares == 1,
          "lost ACK retry idempotent");
    auto changed = publish(1, missing_tile());
    changed.get<Object>()["tiles"].get<Array>()[0].get<Object>()["title"] = Json("Other");
    check(sync_operation(receiver, "one", changed.serialize(), {}, 210).get("code") ==
              Json("revision_conflict"),
          "same revision cannot change");
    reject_prepare = true;
    check(sync_operation(receiver, "one", publish(2, missing_tile()).serialize(), {}, 220).get("code") ==
              Json("invalid_template"),
          "preparation failure rejected");
    check(receiver.entries()[0].snapshot.revision == 1 && receiver.entries()[0].seen == 200,
          "failure does not mutate model or heartbeat");
    reject_prepare = false;
    check(!receiver.expire(15199) && receiver.expire(15200) && receiver.entries()[0].offline,
          "TTL exact 15s boundary");
    auto ping = Json(Object{{"type", Json("ping")}, {"revision", Json(1.)}}).serialize();
    check(sync_operation(receiver, "one", ping, {}, 16000).get("type") == Json("accepted") &&
              !receiver.entries()[0].offline,
          "heartbeat restores offline");
    check(sync_operation(receiver, "one", put, "wrong", 16001).get("code") == Json("hash_mismatch"),
          "bad asset bytes rejected");
    check(sync_operation(receiver, "one", put, body, 16002).get("type") == Json("stored"),
          "checked asset accepted");
    check(receiver.entries()[0].seen == 16000, "upload never renews heartbeat");
    check(sync_operation(receiver, "one", "[]", {}, 16003).get("type") == Json("rejected"),
          "non-object cannot terminate receiver");
    for (auto bad : {"DASH/4\told\n", "{\"type\":\"ping\",\"revision\":1,\"extra\":true}"})
        check(sync_operation(receiver, "one", bad, {}, 16004).get("type") == Json("rejected"),
              "old format and unknown fields refused");
    // Concrete documented resources: cold template miss, then typed SVG miss, then atomic accept.
    auto example = parse(file("docs/dash5/weather/publish.json"));
    example.get<Object>()["revision"] = Json(2.);
    auto missing = sync_operation(receiver, "one", example.serialize(), {}, 17000);
    check(missing.get("code") == Json("missing_resources"), "cold template miss");
    auto store = [&](const char *metadata, const char *data) {
        auto r = sync_operation(receiver, "one", file(metadata), file(data), 17001);
        check(r.get("type") == Json("stored"), "documented byte-exact asset accepted");
    };
    store("docs/dash5/weather/put-template.json", "docs/dash5/weather/template.json");
    missing = sync_operation(receiver, "one", example.serialize(), {}, 17002);
    check(missing.get("code") == Json("missing_resources"),
          "discover typed SVG dependency only after template");
    store("docs/dash5/weather/put-illustration.json", "docs/dash5/weather/illustration.svg");
    check(sync_operation(receiver, "one", example.serialize(), {}, 17003).get("type") == Json("accepted"),
          "accept complete asset graph");
    receiver.disconnect("one");
    check(receiver.hello("new", hello()).get("type") == Json("ready"), "reconnect same instance");
    check(sync_operation(receiver, "new", example.serialize(), {}, 18000).get("type") == Json("accepted"),
          "active resources survive disconnect");
    auto stale = publish(1, missing_tile()).serialize();
    check(sync_operation(receiver, "new", stale, {}, 18001).get("code") == Json("stale_revision"),
          "out-of-order revision rejected");
    // Resource IDs are globally deduplicated, but every service must pay for
    // the complete union it pins. Cached resources cannot bypass its quota.
    auto quota_case = [&](unsigned field_count, std::size_t graph_bytes, bool repeated, bool allowed) {
        unsigned prepared = 0;
        Receiver quota([&](const Snapshot &snapshot, const Resources &) {
            ++prepared;
            return prepared_tiles(snapshot);
        });
        for (auto owner : {"a", "b", "c"}) {
            auto identity = hello();
            identity.get<Object>()["service_id"] = Json(owner);
            check(quota.hello(owner, identity).get("type") == Json("ready"), "quota producer handshake");
        }
        check(sync_operation(quota, "c", publish(1, missing_tile()).serialize(), {}, 100).get("type") ==
                  Json("accepted"),
              "existing presentation before quota admission");
        auto definition = parse(file("apps/weather/assets/template.json"), 32);
        Array fields;
        for (unsigned i = 0; i < field_count; ++i)
            fields.emplace_back(Object{{"name", Json("f" + std::to_string(i))}, {"format", Json("svg")}});
        definition.get<Object>()["fields"] = Json(fields);
        for (auto name : {"texts", "metrics", "images"})
            definition.get<Object>()[name] = Json(Array{});
        const auto template_bytes = definition.serialize();
        const auto body_bytes = graph_bytes - template_bytes.size();
        const unsigned resources = repeated ? 1 : field_count;
        auto store = [&](const char *owner, const std::string &bytes, const char *mime) {
            const auto id = resource_id(bytes);
            const auto operation = Json(Object{{"type", Json("put")},
                                               {"id", Json(id)},
                                               {"media_type", Json(mime)},
                                               {"bytes", Json(double(bytes.size()))}});
            check(sync_operation(quota, owner, operation.serialize(), bytes, 150).get("type") ==
                      Json("stored"),
                  "each contributing producer stays within its own upload quota");
            return id;
        };
        std::vector<std::string> ids;
        for (unsigned i = 0; i < resources; ++i) {
            auto svg = "<svg width='1' height='1'/><!--" + std::to_string(i) + "-->";
            const auto bytes = body_bytes / resources + (i == resources - 1 ? body_bytes % resources : 0);
            check(bytes >= svg.size() && bytes <= 65536, "bounded SVG quota fixture");
            svg.resize(bytes, ' ');
            ids.push_back(store(i % 2 ? "b" : "a", svg, "image/svg+xml"));
        }
        const auto template_id = store("c", template_bytes, "application/vnd.kafel.tile+json");
        Object data;
        for (unsigned i = 0; i < field_count; ++i)
            data["f" + std::to_string(i)] = Json(ids[i % resources]);
        auto tile = missing_tile();
        tile.get<Object>()["state"] = Json("ok");
        tile.get<Object>()["freshness"] = Json("fresh");
        tile.get<Object>()["message"] = Json("");
        tile.get<Object>()["observed_at"] = Json(0.);
        tile.get<Object>()["content"] = Json(Object{{"template", Json(template_id)}, {"data", Json(data)}});
        const auto reply = sync_operation(quota, "c", publish(2, tile).serialize(), {}, 200);
        if (allowed) {
            check(reply.get("type") == Json("accepted") && prepared == 2,
                  "per-service quota accepts exact boundary and deduplicated references");
            std::size_t pinned_bytes = 0;
            for (const auto &[id, resource] : quota.entries()[0].pinned) {
                (void)id;
                pinned_bytes += resource->bytes.size();
            }
            check(pinned_bytes == graph_bytes, "per-service bytes count each cached resource exactly once");
        } else {
            check(reply.get("code") == Json("limit_exceeded") && prepared == 1,
                  "cached cross-service resources cannot bypass admission quota");
            check(quota.entries()[0].snapshot.revision == 1 && quota.entries()[0].seen == 100 &&
                      quota.generation() == 1,
                  "quota rejection leaves old snapshot and heartbeat unchanged");
        }
    };
    quota_case(24, 1440000, false, false);
    quota_case(16, 1048576, false, true);
    quota_case(16, 1048577, false, false);
    quota_case(24, 64000, true, true);
    std::puts("PASS DASH/5 strict JSON, SHA256 vectors, all stream splits, atomicity, resources, revisions, "
              "retry, reconnect and exact TTL");
}

namespace {
void test_commit_allocation_failure() {
    for (bool asynchronous : {false, true})
        for (bool replacing : {false, true}) {
            bool succeeded = false;
            unsigned failures = 0;
            for (std::size_t allocation = 0; allocation < 256 && !succeeded; ++allocation) {
                bool inject = false;
                const auto old_prepared = std::make_shared<const PreparedTiles>(
                    std::vector<std::shared_ptr<const ui::CardFrame>>(1));
                const auto next_prepared = std::make_shared<const PreparedTiles>(
                    std::vector<std::shared_ptr<const ui::CardFrame>>(1));
                Receiver receiver(
                    [&](const Snapshot &, const Resources &) -> std::optional<Prepared> {
                        if (inject && asynchronous)
                            return std::nullopt;
                        if (inject)
                            fail_allocation_after(allocation);
                        return inject ? next_prepared : old_prepared;
                    },
                    {},
                    [&]() -> Prepared {
                        fail_allocation_after(allocation);
                        return next_prepared;
                    });
                receiver.hello("one", hello());
                if (replacing) {
                    auto old =
                        sync_operation(receiver, "one", publish(1, missing_tile()).serialize(), {}, 100);
                    check(old.get("type") == Json("accepted"), "atomic test old revision");
                }
                const auto revision = replacing ? 2u : 1u;
                auto tile = missing_tile();
                tile.get<Object>()["title"] = Json("New tile");
                tile.get<Object>()["message"] = Json("New source failure");
                const auto wire = publish(revision, tile).serialize();
                inject = true;
                auto reply = receiver.operation("one", wire, {}, 200);
                if (asynchronous) {
                    check(!reply, "atomic test starts actual pending commit path");
                    reply = receiver.finish_preparation(250);
                }
                const bool failed = stop_allocation_failure();
                check(reply.has_value(), "commit produced a framed reply");
                const auto decoded = decoded_reply(*reply);
                if (failed) {
                    ++failures;
                    check(decoded.get("code") == Json("limit_exceeded"),
                          "allocation failure returns precise rejection");
                    check(receiver.generation() == (replacing ? 1u : 0u) &&
                              receiver.entries().size() == (replacing ? 1u : 0u),
                          "rejection never installs a new snapshot");
                    if (replacing)
                        check(receiver.entries()[0].snapshot.revision == 1 &&
                                  receiver.entries()[0].seen == 100 &&
                                  receiver.entries()[0].presentation == old_prepared &&
                                  receiver.entries()[0].snapshot.tiles[0].title == "Test" &&
                                  receiver.entries()[0].snapshot.tiles[0].message == "Brak danych",
                              "every failed allocation preserves old revision, TTL and presentation");
                } else {
                    check(decoded.get("type") == Json("accepted") &&
                              receiver.entries()[0].snapshot.revision == revision,
                          "all fallible work finishes before visible mutation");
                    succeeded = true;
                }
            }
            check(succeeded && failures >= 10,
                  "failure injection covers reply construction, serialization and container growth");
        }
    std::puts("PASS atomic publication: every commit allocation failure, insert/update, sync/async, old TTL "
              "and payload");
}
void test_preparation_completion() {
    unsigned started = 0, finished = 0;
    bool fail_completion = false;
    Snapshot candidate;
    Receiver receiver(
        [&](const Snapshot &snapshot, const Resources &) -> std::optional<Prepared> {
            ++started;
            candidate = snapshot;
            return std::nullopt;
        },
        {},
        [&]() -> Prepared {
            ++finished;
            if (fail_completion)
                throw std::invalid_argument("invalid_template");
            return prepared_tiles(candidate);
        });
    receiver.hello("one", hello());
    auto first = publish(1, missing_tile()).serialize();
    check(!receiver.operation("one", first, {}, 100), "preparation has no protocol reply until completion");
    check(receiver.entries().empty() && receiver.generation() == 0,
          "pending candidate never becomes visible");
    const auto accepted = receiver.finish_preparation(150);
    check(accepted && decoded_reply(*accepted).get("type") == Json("accepted"),
          "completion accepts pending revision");
    check(started == 1 && finished == 1, "completion never reruns preparation");
    check(receiver.entries()[0].seen == 150, "TTL begins when preparation is committed");
    check(receiver.entries()[0].snapshot.tiles[0].state == TileState::Error &&
              receiver.entries()[0].snapshot.tiles[0].freshness == Freshness::Missing,
          "wire enum boundary");
    auto second = publish(2, missing_tile()).serialize();
    check(!receiver.operation("one", second, {}, 200), "next revision starts preparation");
    fail_completion = true;
    const auto rejected = receiver.finish_preparation(250);
    check(rejected && decoded_reply(*rejected).get("code") == Json("invalid_template") &&
              decoded_reply(*rejected).get("revision") == Json(2.),
          "completion preserves error and revision");
    check(receiver.entries()[0].snapshot.revision == 1 && receiver.entries()[0].seen == 150 &&
              receiver.generation() == 1,
          "preparation failure leaves old data, TTL and generation");
    check(!receiver.operation("one", second, {}, 300), "retry starts a new candidate");
    receiver.disconnect("one");
    check(receiver.preparing(), "Receiver keeps cancelled work owned until physical completion");
    check(!receiver.finish_preparation(350) && !receiver.preparing() && finished == 3,
          "cancelled failing worker is drained without a reply or visible mutation");
    receiver.hello("one", hello());
    for (auto field : {"state", "freshness"}) {
        auto invalid = missing_tile();
        invalid.get<Object>()[field] = Json("typo");
        auto reply = sync_operation(receiver, "one", publish(2, invalid).serialize(), {}, 400);
        check(reply.get("code") == Json("invalid_data"), "unknown enum rejected before preparation");
    }
    check(started == 3, "invalid enums never reach preparation");
    Receiver wrong([](const Snapshot &, const Resources &) -> Prepared {
        return std::make_shared<const PreparedTiles>();
    });
    wrong.hello("one", hello());
    check(sync_operation(wrong, "one", first, {}, 1).get("code") == Json("invalid_data") &&
              wrong.entries().empty(),
          "wrong prepared cardinality cannot be committed");
}

} // namespace
void test_dash5() {
    test_dash5_framing();
    test_dash5_receiver();
    test_preparation_completion();
    test_commit_allocation_failure();
}
