#include "dash5/bundle.hpp"
#include "dash5/unix.hpp"
#include "dashboard/async_compiler.hpp"
#include <atomic>
#include <condition_variable>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <unistd.h>
void check(bool, const char *);
namespace {
using namespace kafel;
using namespace kafel::dash5;
struct Completion {
    int fd[2];
    Completion() {
        if (pipe(fd) < 0)
            throw std::runtime_error("test completion pipe");
        for (int value : fd)
            if (fcntl(value, F_SETFL, O_NONBLOCK) < 0)
                throw std::runtime_error("test pipe flags");
    }
    ~Completion() {
        close(fd[0]);
        close(fd[1]);
    }
    void wait() {
        pollfd event{fd[0], POLLIN, 0};
        check(poll(&event, 1, 3000) == 1 && (event.revents & POLLIN),
              "real compiler notifies through the physical pipe");
        char bytes[4];
        check(read(fd[0], bytes, sizeof bytes) == 1, "exactly one completion notification per worker");
    }
};
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    void pause() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(3), [&] { return released; }))
            throw std::runtime_error("test worker gate timeout");
    }
    void wait() {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }),
              "physical worker entered gate");
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
std::atomic<Gate *> delivery_gate{nullptr};
std::string file(const char *path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}
Json decode(const std::optional<std::string> &reply) {
    check(bool(reply), "ready receiver reply");
    return parse(std::string_view(*reply).substr(4));
}
} // namespace
namespace kafel {
void before_service_delivery_for_test() {
    if (auto *gate = delivery_gate.load())
        gate->pause();
}
} // namespace kafel
void test_async_compiler() {
    using namespace kafel;
    using namespace kafel::dash5;
    const auto assets = asset_bundle(file("apps/weather/assets/bundle.json"));
    Json fields(Object{{"temperature", Json(13.)},
                       {"apparent_temperature", Json(11.)},
                       {"wind_ms", Json(5.)},
                       {"precipitation_mm", Json(0.)},
                       {"description", Json("Sucho")},
                       {"illustration", Json(assets.svg.at("partly-cloudy-day"))}});
    auto publication = [&](unsigned revision, const Json &data) {
        return Json(Object{{"type", Json("publish")},
                           {"revision", Json(double(revision))},
                           {"tiles", Json(Array{Json(Object{
                                         {"id", Json("weather")},
                                         {"title", Json("Pogoda")},
                                         {"state", Json("ok")},
                                         {"freshness", Json("fresh")},
                                         {"attention", Json(false)},
                                         {"observed_at", Json(1.)},
                                         {"message", Json("")},
                                         {"content", Json(Object{{"template", Json(assets.template_id)},
                                                                 {"data", data}})}})})}})
            .serialize();
    };
    Completion completion;
    AsyncCompiler compiler(completion.fd[1]);
    Gate gate;
    std::atomic<unsigned> executions = 0;
    compiler.before_compile_for_test([&] {
        if (++executions == 1)
            gate.pause();
    });
    Receiver receiver([&](const Snapshot &snapshot,
                          const Resources &resources) { return compiler.start(snapshot, resources); },
                      [&](const Resource &resource) { return compiler.validate(resource); },
                      [&] { return compiler.complete(); });
    const Json greeting(Object{{"type", Json("hello")},
                               {"service_id", Json("test")},
                               {"name", Json("Test")},
                               {"instance", Json("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")}});
    receiver.hello("owner", greeting);
    for (const auto &id : {assets.template_id, assets.svg.at("partly-cloudy-day")}) {
        const auto &resource = *assets.resources.at(id);
        const auto put = Json(Object{{"type", Json("put")},
                                     {"id", Json(id)},
                                     {"media_type", Json(resource.media_type)},
                                     {"bytes", Json(double(resource.bytes.size()))}})
                             .serialize();
        check(decode(receiver.operation("owner", put, resource.bytes, 1)).get("type") == Json("stored"),
              "real compiler validates uploaded assets");
    }
    auto wire = publication(1, fields);
    check(!receiver.operation("owner", wire, {}, 10), "real rendering is asynchronous");
    gate.wait();
    bool hook_refused = false;
    try {
        compiler.before_compile_for_test([] {});
    } catch (const std::logic_error &error) {
        hook_refused = std::string_view(error.what()) == "test hook changed during preparation";
    }
    check(hook_refused, "active worker synchronization hook cannot be replaced concurrently");
    receiver.disconnect("owner");
    receiver.hello("next", greeting);
    check(decode(receiver.operation("next", wire, {}, 20)).get("code") == Json("service_busy") &&
              executions == 1,
          "cancelled physical job blocks a second start until drained");
    gate.release();
    completion.wait();
    check(!receiver.finish_preparation(30) && receiver.entries().empty() && !receiver.preparing(),
          "cancelled real raster result never becomes visible");
    check(!receiver.operation("next", wire, {}, 40), "same real worker is reusable after cancellation");
    completion.wait();
    check(decode(receiver.finish_preparation(50)).get("type") == Json("accepted") && executions == 2,
          "real worker completes exactly once per candidate");
    TileCompiler reference;
    auto expected = reference.compile(receiver.entries()[0].snapshot, assets.resources);
    check(receiver.entries()[0].presentation->frames[0]->same_visuals(*expected->frames[0]),
          "threaded compiler uses actual TTF/SVG rendering, identical to synchronous output");
    {
        using Clock = std::chrono::steady_clock;
        const auto snapshot = receiver.entries()[0].snapshot;
        for (auto elapsed : {std::chrono::milliseconds(2000), std::chrono::milliseconds(2001)}) {
            TileCompiler budgeted;
            unsigned clock_reads = 0;
            budgeted.clock_for_test = [&] {
                return Clock::time_point{} + (++clock_reads == 1 ? Clock::duration{} : elapsed);
            };
            bool timed_out = false;
            dash5::Prepared result;
            try {
                result = budgeted.compile(snapshot, assets.resources);
            } catch (const std::invalid_argument &error) {
                check(std::string_view(error.what()) == "prepare_timeout", "exact budget error code");
                timed_out = true;
            }
            check(clock_reads == 2 && budgeted.costs().frames == 1,
                  "soft budget is checked after actual rendering, not before or during it");
            check(timed_out == (elapsed > std::chrono::milliseconds(2000)),
                  "two seconds is accepted; exceeding the soft budget rejects the result");
            check(timed_out ? !result : result->frames[0]->same_visuals(*expected->frames[0]),
                  "late frame is not returned; on-time frame retains identical pixels");
            budgeted.clock_for_test = {};
            auto recovered = budgeted.compile(snapshot, assets.resources);
            check(recovered->frames[0]->same_visuals(*expected->frames[0]),
                  "compiler remains usable after a soft budget rejection");
            const auto renders = budgeted.costs().frames;
            clock_reads = 0;
            budgeted.clock_for_test = [&] {
                return Clock::time_point{} +
                       (++clock_reads == 1 ? Clock::duration{} : std::chrono::milliseconds(2001));
            };
            bool cached_timeout = false;
            try {
                budgeted.compile(snapshot, assets.resources);
            } catch (const std::invalid_argument &error) {
                cached_timeout = std::string_view(error.what()) == "prepare_timeout";
            }
            check(cached_timeout && clock_reads == 2 && budgeted.costs().frames == renders,
                  "cached tiles also respect the soft budget without another render");
        }
    }
    auto invalid = fields;
    invalid.get<Object>()["temperature"] = Json(101.);
    check(!receiver.operation("next", publication(2, invalid), {}, 60), "invalid data reaches real worker");
    completion.wait();
    check(decode(receiver.finish_preparation(70)).get("code") == Json("invalid_data") &&
              receiver.entries()[0].snapshot.revision == 1 && receiver.entries()[0].seen == 50,
          "real worker exception preserves old snapshot and TTL");
    check(!receiver.operation("next", publication(2, fields), {}, 80),
          "worker restarts after rendering exception");
    completion.wait();
    check(decode(receiver.finish_preparation(90)).get("type") == Json("accepted") && executions == 4,
          "rendering failure leaves no orphan job or completion notification");
    check(receiver.entries()[0].presentation->frames[0] == expected->frames[0] ||
              receiver.entries()[0].presentation->frames[0]->same_visuals(*expected->frames[0]),
          "recovered real frame");
    {
        Completion done;
        Gate stop;
        auto joining = std::make_unique<AsyncCompiler>(done.fd[1]);
        joining->before_compile_for_test([&] { stop.pause(); });
        check(!joining->start(receiver.entries()[0].snapshot, assets.resources),
              "destructor test starts real work");
        stop.wait();
        std::promise<void> destroying;
        auto entered = destroying.get_future();
        auto destroyed = std::async(std::launch::async, [&] {
            destroying.set_value();
            joining.reset();
        });
        entered.get();
        check(destroyed.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
              "destructor joins active worker");
        stop.release();
        destroyed.get();
        done.wait();
    }
    {
        char directory[] = "/tmp/kafel-delivery-XXXXXX";
        check(mkdtemp(directory) != nullptr, "delivery socket fixture");
        const auto socket = std::string(directory) + "/socket";
        Gate delivery;
        delivery_gate.store(&delivery);
        {
            ServiceFeed feed(socket);
            Publisher publisher(socket, "test", "Test", assets.resources);
            const auto tiles = parse(publication(1, fields)).get("tiles").serialize();
            auto acknowledged = std::async(std::launch::async, [&] { return publisher.publish(tiles); });
            delivery.wait();
            check(feed.consume()->empty(), "delivery gate holds old UI snapshot");
            check(acknowledged.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout,
                  "producer cannot receive ACK before matching UI snapshot is delivered");
            delivery.release();
            check(acknowledged.get() == 0 && feed.consume()->size() == 1,
                  "successful ACK observes the matching checked service snapshot");
        }
        delivery_gate.store(nullptr);
        unlink((std::string(directory) + "/lock").c_str());
        rmdir(directory);
    }
    std::puts("PASS real AsyncCompiler: TTF/SVG, physical pipe, deterministic cancellation, recovery, "
              "failure, destructor join and delivery-before-ACK");
}
