#pragma once
#include "dash5/protocol.hpp"
#include "service_feed.hpp"
#include "ui/card.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef KAFEL_ASYNC_TEST_HOOKS
#include <functional>
#endif
namespace kafel {
class TileCompiler {
    struct Cached {
        std::weak_ptr<const ui::CardFrame> frame;
        std::uint64_t used = 0;
    };
    std::map<std::string, Cached, std::less<>> cache;
    std::uint64_t accessed = 0;
    struct CachedTemplate {
        std::shared_ptr<const ui::CardTemplate> compiled;
        std::uint64_t used;
    };
    std::mutex templates_mutex;
    std::map<std::string, CachedTemplate, std::less<>> templates;
    std::uint64_t template_accessed = 0;
    ui::CardRenderCache render_cache;
    struct CostCounters {
        std::atomic<std::size_t> templates{0}, fonts{0}, svg{0}, frames{0};
    } costs_;

  public:
    std::shared_ptr<const ui::CardTemplate> template_for(const dash5::Resource &);
    dash5::Prepared compile(const dash5::Snapshot &, const dash5::Resources &);
    ServiceFeed::PreparationCosts costs() const noexcept;
#ifdef KAFEL_ASYNC_TEST_HOOKS
    // Deterministic budget checks around real preparation, without sleeping.
    std::function<std::chrono::steady_clock::time_point()> clock_for_test;
#endif
};
// One physical worker. Receiver owns publication/cancellation; this class only
// starts a job and drains it once. There is no second candidate queue.
class AsyncCompiler {
  public:
    explicit AsyncCompiler(int completion_fd);
    ~AsyncCompiler();
    AsyncCompiler(const AsyncCompiler &) = delete;
    AsyncCompiler &operator=(const AsyncCompiler &) = delete;
    std::vector<std::string> validate(const dash5::Resource &);
    std::optional<dash5::Prepared> start(const dash5::Snapshot &, const dash5::Resources &);
    dash5::Prepared complete();
    void finish();
    ServiceFeed::PreparationCosts costs() const noexcept;
#ifdef KAFEL_ASYNC_TEST_HOOKS
    // Synchronization only: tests pause the actual worker before real rendering.
    void before_compile_for_test(std::function<void()> callback) {
        if (job_.valid())
            throw std::logic_error("test hook changed during preparation");
        before_compile_ = std::move(callback);
    }
#endif
  private:
    TileCompiler compiler_;
    std::future<dash5::Prepared> job_;
    std::thread worker_;
    int completion_fd_;
#ifdef KAFEL_ASYNC_TEST_HOOKS
    std::function<void()> before_compile_;
#endif
};
} // namespace kafel
