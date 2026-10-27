#pragma once
#include "system/system_metrics.hpp"
namespace kafel::linux_device {
// Called once per second by the app; slower identity/network/storage refresh
// is cached for 10 seconds. No subprocesses, threads or writes to the system.
class LinuxSystemProbe {
  public:
    SystemSnapshot sample(std::uint64_t now_ms) noexcept;

  private:
    SystemSnapshot cached_;
    std::optional<CpuTicks> previous_cpu_;
    std::uint64_t slow_at_ = 0;
    bool sampled_ = false;
    void refresh_slow() noexcept;
};
} // namespace kafel::linux_device
