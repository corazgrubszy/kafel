#pragma once
#include "dashboard_view.hpp"
#include <memory>
namespace kafel {
// Owns the I/O/preparation worker. The UI receives immutable, complete snapshots.
class ServiceFeed {
  public:
    explicit ServiceFeed(std::string socket_path);
    ~ServiceFeed();
    ServiceFeed(const ServiceFeed &) = delete;
    ServiceFeed &operator=(const ServiceFeed &) = delete;
    struct PreparationCosts {
        std::size_t templates = 0, fonts = 0, svg = 0, frames = 0;
    };
    PreparationCosts preparation_costs() const noexcept;
    int descriptor() const noexcept;
    std::shared_ptr<const std::vector<ServiceCard>> consume();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace kafel
