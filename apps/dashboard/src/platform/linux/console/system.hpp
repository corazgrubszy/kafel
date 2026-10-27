#pragma once
#include "isolation.hpp"
namespace kafel::console {
// Linux UAPI is confined to system.cpp. Neither tests nor UI open real VTs.
class LinuxConsole final : public Console {
  public:
    explicit LinuxConsole(std::string lcd);
    ~LinuxConsole() override;
    Bindings bindings() override;
    void bind(std::string_view, bool) override;
    Mapping maps() override;
    void map(unsigned, unsigned) override;
    std::optional<unsigned> hdmi() override;

  private:
    int fd_ = -1;
};
std::string detect_lcd();
} // namespace kafel::console
