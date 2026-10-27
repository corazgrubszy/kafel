#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace kafel::console {
inline constexpr unsigned console_count = 63, framebuffer_count = 32;
inline constexpr std::string_view hdmi_mode = "video=HDMI-A-1:640x480@60D";
using Bindings = std::map<std::string, bool>;
using Mapping = std::vector<std::uint32_t>;
unsigned framebuffer_index(std::string_view path);
bool console_name(std::string_view) noexcept;
struct Snapshot {
    std::string lcd;
    Bindings bindings;
    Mapping maps;
    bool operator==(const Snapshot &) const = default;
};
struct Report {
    std::string lcd;
    std::optional<unsigned> hdmi;
    bool operator==(const Report &) const = default;
};
// Only this adapter can touch console devices. Policy tests use a fake adapter.
class Console {
  public:
    const std::string lcd;
    explicit Console(std::string path);
    virtual ~Console() = default;
    virtual Bindings bindings() = 0;
    virtual void bind(std::string_view, bool) = 0;
    virtual Mapping maps() = 0;
    virtual void map(unsigned tty, unsigned framebuffer) = 0;
    virtual std::optional<unsigned> hdmi() = 0;
};
Snapshot snapshot(Console &);
void restore(Console &, const Snapshot &);
// On failure, restores this snapshot; reports both errors if rollback fails.
Report isolate(Console &, const Snapshot &);
// Shared production discovery code, testable against an isolated sysfs fixture.
std::string read_text(const std::filesystem::path &);
Bindings framebuffer_bindings(const std::filesystem::path &root);
std::optional<unsigned> find_hdmi(const std::filesystem::path &root, std::string_view lcd);
std::string encode(const Snapshot &);
std::string encode(const Report &);
Snapshot decode_snapshot(std::string_view);
// Private, locked state directory. Atomic writes; no symlinks or shared files.
class StateStore {
  public:
    explicit StateStore(std::filesystem::path directory);
    ~StateStore();
    StateStore(const StateStore &) = delete;
    StateStore &operator=(const StateStore &) = delete;
    Snapshot original() const;
    void save_original(const Snapshot &);
    void save_report(const Report &);
    void clear();

  private:
    std::filesystem::path directory_;
    int directory_fd_ = -1, lock_ = -1;
    std::optional<std::string> read(const char *) const;
    void save(const char *, std::string_view);
};
Report isolate_and_record(Console &, StateStore &);
} // namespace kafel::console
