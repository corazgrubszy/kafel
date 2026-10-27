#include "async_compiler.hpp"
#include "ui/svg.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <unistd.h>
namespace kafel {
using namespace dash5;
extern "C" const unsigned char *ui_sans_regular_data();
extern "C" std::size_t ui_sans_regular_size();
extern "C" const unsigned char *ui_sans_bold_data();
extern "C" std::size_t ui_sans_bold_size();
std::shared_ptr<const ui::CardTemplate> TileCompiler::template_for(const Resource &resource) {
    // The I/O validator and preparation worker share only immutable templates.
    // This lock is never acquired by the UI/render thread.
    std::lock_guard lock(templates_mutex);
    if (auto it = templates.find(resource.id); it != templates.end()) {
        it->second.used = ++template_accessed;
        return it->second.compiled;
    }
    constexpr std::array<std::string_view, 2> faces = {"ibm-plex-sans/regular", "ibm-plex-sans/bold"};
    auto compiled = std::make_shared<ui::CardTemplate>(resource.bytes, faces);
    if (templates.size() >= tile_limit * 2) {
        auto oldest = std::min_element(templates.begin(), templates.end(), [](const auto &a, const auto &b) {
            return a.second.used < b.second.used;
        });
        templates.erase(oldest);
    }
    templates.emplace(resource.id, CachedTemplate{compiled, ++template_accessed});
    ++costs_.templates;
    return compiled;
}
dash5::Prepared TileCompiler::compile(const Snapshot &snapshot, const Resources &resources) {
    for (auto it = cache.begin(); it != cache.end();)
        if (it->second.frame.expired())
            it = cache.erase(it);
        else
            ++it;
    std::vector<std::shared_ptr<const ui::CardFrame>> frames;
    frames.reserve(snapshot.tiles.size());
    const auto now = [&] {
#ifdef KAFEL_ASYNC_TEST_HOOKS
        if (clock_for_test)
            return clock_for_test();
#endif
        return std::chrono::steady_clock::now();
    };
    const auto start = now();
    for (const auto &t : snapshot.tiles) {
        if (t.content.is<picojson::null>()) {
            frames.push_back({});
            continue;
        }
        auto id = t.content.get("template").get<std::string>();
        auto tpl = resources.at(id);
        if (tpl->media_type != "application/vnd.kafel.tile+json")
            throw std::invalid_argument("invalid_template");
        const auto &data = t.content.get("data");
        auto compiled = template_for(*tpl);
        auto key = id + compiled->visual_key(data);
        auto found = cache.find(key);
        auto frame =
            found == cache.end() ? std::shared_ptr<const ui::CardFrame>{} : found->second.frame.lock();
        if (!frame) {
            std::size_t retained = 0;
            for (const auto &prepared : frames) {
                // Missing/error tiles deliberately have no prepared image.
                if (prepared)
                    retained += prepared->storage_bytes();
            }
            frame = compiled->prepare(
                data,
                [&](std::string_view hash) -> std::string_view {
                    auto it = resources.find(hash);
                    if (it == resources.end() || it->second->media_type != "image/svg+xml")
                        throw std::invalid_argument("invalid_svg");
                    return it->second->bytes;
                },
                [](std::string_view face) -> std::span<const unsigned char> {
                    if (face == "ibm-plex-sans/regular")
                        return {ui_sans_regular_data(), ui_sans_regular_size()};
                    if (face == "ibm-plex-sans/bold")
                        return {ui_sans_bold_data(), ui_sans_bold_size()};
                    throw std::invalid_argument("invalid_template");
                },
                render_cache, 8388608 - retained, 4);
            const auto stats = render_cache.stats();
            costs_.fonts.store(stats.font_preparations);
            costs_.svg.store(stats.svg_rasterizations);
            costs_.frames.store(stats.frame_renders);
            // Subpixel/raw-data changes may render to exactly the same pixels.
            for (const auto &[unused, entry] : cache) {
                (void)unused;
                if (auto previous = entry.frame.lock(); previous && previous->same_visuals(*frame)) {
                    frame = previous;
                    break;
                }
            }
            cache[key] = {frame, ++accessed};
        }
        if (found != cache.end())
            found->second.used = ++accessed;
        frames.push_back(std::move(frame));
        // Soft admission budget, checked after each complete tile. Rendering
        // is not interrupted; a slow tile can exceed it before rejection.
        if (now() - start > std::chrono::seconds(2))
            throw std::invalid_argument("prepare_timeout");
    }
    for (auto it = cache.begin(); it != cache.end();)
        if (it->second.frame.expired())
            it = cache.erase(it);
        else
            ++it;
    while (cache.size() > 26) {
        auto oldest = std::min_element(cache.begin(), cache.end(), [](const auto &a, const auto &b) {
            return a.second.used < b.second.used;
        });
        cache.erase(oldest);
    }
    return std::make_shared<const PreparedTiles>(std::move(frames));
}
ServiceFeed::PreparationCosts TileCompiler::costs() const noexcept {
    return {costs_.templates.load(), costs_.fonts.load(), costs_.svg.load(), costs_.frames.load()};
}
AsyncCompiler::AsyncCompiler(int completion_fd) : completion_fd_(completion_fd) {
    if (completion_fd < 0)
        throw std::invalid_argument("invalid preparation completion fd");
}
AsyncCompiler::~AsyncCompiler() { finish(); }
void AsyncCompiler::finish() {
    if (worker_.joinable())
        worker_.join();
}
std::vector<std::string> AsyncCompiler::validate(const Resource &resource) {
    if (resource.media_type == "image/svg+xml") {
        ui::SvgDocument svg(resource.bytes);
        return {};
    }
    const auto compiled = compiler_.template_for(resource);
    const auto fields = compiled->svg_fields();
    return std::vector<std::string>(fields.begin(), fields.end());
}
ServiceFeed::PreparationCosts AsyncCompiler::costs() const noexcept { return compiler_.costs(); }
std::optional<Prepared> AsyncCompiler::start(const Snapshot &snapshot, const Resources &resources) {
    if (job_.valid())
        throw std::logic_error("preparation worker has not been drained");
    if (std::all_of(snapshot.tiles.begin(), snapshot.tiles.end(),
                    [](const Tile &tile) { return tile.state == TileState::Error; })) {
        return std::make_shared<const PreparedTiles>(
            std::vector<std::shared_ptr<const ui::CardFrame>>(snapshot.tiles.size()));
    }
    std::promise<Prepared> promise;
    auto future = promise.get_future();
    // Construct the physical thread before publishing job_. A failed thread
    // creation leaves this compiler idle and reusable.
    std::thread worker([this, snapshot, resources, promise = std::move(promise)]() mutable {
        try {
#ifdef KAFEL_ASYNC_TEST_HOOKS
            if (before_compile_)
                before_compile_();
#endif
            promise.set_value(compiler_.compile(snapshot, resources));
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
        char byte = 1;
        while (::write(completion_fd_, &byte, 1) < 0 && errno == EINTR) {
        }
    });
    job_ = std::move(future);
    worker_ = std::move(worker);
    return std::nullopt;
}
Prepared AsyncCompiler::complete() {
    if (!job_.valid() || job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        throw std::logic_error("preparation completed without a ready worker");
    finish();
    return job_.get();
}
} // namespace kafel
