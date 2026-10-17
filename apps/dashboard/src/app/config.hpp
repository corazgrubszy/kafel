#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace kafel::app {
enum class Command { Run,Help,Version,Check,CheckInput,SystemInfo,DisplayInfo };
struct DashboardConfig {
    Command command=Command::Run;
    std::string framebuffer="auto",touch="auto",socket;
    std::uint64_t idle_timeout_ms=60000;
};
using Environment = const char* (*)(const char*);
// Pure parsing: no device access. CLI overrides environment, then defaults.
DashboardConfig parse_config(std::span<const std::string_view> arguments,Environment environment=nullptr);
}
