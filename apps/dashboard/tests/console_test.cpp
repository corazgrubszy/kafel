#include "platform/linux/console/isolation.hpp"
#include "json/strict.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
void check(bool, const char *);
namespace {
using namespace kafel::console;
struct FakeConsole final : Console {
    Bindings bound{{"vtcon1", true}};
    Mapping mapping = Mapping(console_count, 7);
    std::optional<unsigned> target;
    unsigned writes = 0, fail_at = 0, reads = 0;
    bool refuse_bind = false, ignored_bind = false, wrong_verification = false, ignore_maps = false;
    explicit FakeConsole(std::optional<unsigned> hdmi = {}) : Console("/dev/fb7"), target(hdmi) {}
    Bindings bindings() override { return bound; }
    void bind(std::string_view name, bool value) override {
        if (refuse_bind)
            throw std::runtime_error("unbind forbidden");
        if (!ignored_bind)
            bound[std::string(name)] = value;
    }
    Mapping maps() override {
        auto result = mapping;
        if (++reads == 2 && wrong_verification)
            result[3] = 7;
        return result;
    }
    void map(unsigned tty, unsigned fb) override {
        if (++writes == fail_at)
            throw std::runtime_error("injected map error");
        if (!ignore_maps)
            mapping.at(tty - 1) = fb;
    }
    std::optional<unsigned> hdmi() override { return target; }
};
template <class F> void rejects(F operation, std::string_view message = {}) {
    bool caught = false;
    try {
        operation();
    } catch (const std::exception &error) {
        caught = true;
        check(std::string_view(error.what()).find(message) != std::string_view::npos,
              "console error preserves cause");
    }
    check(caught, "unsafe console operation rejected");
}
struct Temporary {
    std::filesystem::path path;
    Temporary() {
        char name[] = "/tmp/kafel-console-test.XXXXXX";
        auto p = mkdtemp(name);
        check(p, "console temporary directory");
        path = p;
    }
    ~Temporary() { std::filesystem::remove_all(path); }
};
void put(const std::filesystem::path &path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << value;
}
} // namespace
void test_console() {
    using namespace kafel::console;
    {
        FakeConsole c;
        const auto before = snapshot(c);
        check(isolate(c, before) == Report{c.lcd, {}}, "headless mode detaches fbcon");
        check(!c.bound.at("vtcon1") && c.mapping == before.maps && c.writes == 0,
              "headless isolation never rewrites console mappings");
        check(isolate(c, snapshot(c)).hdmi == std::nullopt,
              "detached isolation is idempotent without mapping ioctls");
        restore(c, before);
        check(snapshot(c) == before, "explicit restore recovers original routing");
    }
    {
        FakeConsole c(1);
        const auto before = snapshot(c);
        check(isolate(c, before).hdmi == 1 && c.mapping == Mapping(63, 1), "HDMI receives all 63 VTs");
        check(c.bound.at("vtcon1"), "HDMI isolation keeps the text console usable");
        restore(c, before);
        check(snapshot(c) == before, "HDMI routing is reversible");
    }
    {
        FakeConsole c(1);
        const auto before = snapshot(c);
        (void)isolate(c, before);
        c.ignore_maps = true;
        rejects([&] { restore(c, before); }, "verification");
    }
    for (auto target : {7u, 32u}) {
        FakeConsole c(target);
        const auto before = snapshot(c);
        rejects([&] { (void)isolate(c, before); });
        check(snapshot(c) == before, "LCD/invalid target never changes original state");
    }
    {
        FakeConsole c(1);
        c.fail_at = 4;
        const auto before = snapshot(c);
        rejects([&] { (void)isolate(c, before); }, "injected");
        check(snapshot(c) == before, "partial ioctl failure restores every prior mapping and binding");
    }
    {
        FakeConsole c(1);
        c.wrong_verification = true;
        const auto before = snapshot(c);
        rejects([&] { (void)isolate(c, before); }, "verification");
        check(c.mapping == before.maps, "readback mismatch rolls mapping back");
    }
    {
        FakeConsole c(1);
        c.mapping.back() = UINT32_MAX;
        const auto before = snapshot(c);
        check(!isolate(c, before).hdmi && !c.bound.at("vtcon1") && c.writes == 0,
              "unmapped console falls back to detach without mapping writes");
    }
    {
        FakeConsole c;
        c.refuse_bind = true;
        rejects([&] { (void)isolate(c, snapshot(c)); }, "restore failed");
        c.refuse_bind = false;
        c.ignored_bind = true;
        rejects([&] { (void)isolate(c, snapshot(c)); }, "still attached");
    }
    for (auto name : {"/dev/fb", "/dev/fb32", "/dev/fb1/../fb2", "/tmp/fb1", "/dev/fb99999999999999"})
        rejects([&] { (void)framebuffer_index(name); });
    check(!console_name("vtcon1/../../other") && !console_name("vtcon"),
          "console names cannot traverse paths");
    {
        Temporary fs;
        const auto root = fs.path;
        put(root / "proc/cmdline", hdmi_mode);
        put(root / "sys/class/graphics/fb1/name", "vc4drmfb\n");
        put(root / "sys/class/vtconsole/vtcon1/name", "(M) frame buffer device\n");
        put(root / "sys/class/vtconsole/vtcon1/bind", "1\n");
        std::filesystem::create_directories(root / "devices/gpu");
        std::filesystem::create_directory_symlink(root / "devices/gpu",
                                                  root / "sys/class/graphics/fb1/device");
        std::filesystem::create_directories(root / "sys/class/drm/card0");
        std::filesystem::create_directory_symlink(root / "devices/gpu", root / "sys/class/drm/card0/device");
        std::filesystem::create_directories(root / "sys/class/drm/card0-HDMI-A-1");
        check(find_hdmi(root, "/dev/fb7") == 1, "HDMI discovery requires exact DRM device association");
        check(!find_hdmi(root, "/dev/fb1"), "HDMI discovery never returns the LCD");
        check(framebuffer_bindings(root) == Bindings{{"vtcon1", true}},
              "framebuffer console is identified by name");
        put(root / "proc/cmdline", "console=tty2");
        check(!find_hdmi(root, "/dev/fb7"), "connector alone does not prove forced headless HDMI");
        put(root / "proc/cmdline", hdmi_mode);
        put(root / "sys/class/graphics/fb1/name", "unknown\n");
        check(!find_hdmi(root, "/dev/fb7"), "framebuffer number alone does not identify HDMI");
        put(root / "sys/class/graphics/fb1/name", "vc4drmfb\n");
        put(root / "sys/class/graphics/fb2/name", "vc4drmfb\n");
        std::filesystem::create_directory_symlink(root / "devices/gpu",
                                                  root / "sys/class/graphics/fb2/device");
        rejects([&] { (void)find_hdmi(root, "/dev/fb7"); }, "Ambiguous");
        put(root / "sys/class/vtconsole/vtcon1/bind", "2\n");
        rejects([&] { (void)framebuffer_bindings(root); });
    }
    {
        Temporary fs;
        FakeConsole c(1);
        const auto original = snapshot(c);
        check(decode_snapshot(encode(original)) == original,
              "snapshot uses the existing boot-local JSON format");
        StateStore store(fs.path / "state");
        rejects([&] { StateStore duplicate(fs.path / "state"); }, "locked");
        store.save_original(original);
        auto changed = original;
        changed.maps.assign(63, 1);
        store.save_original(changed);
        check(store.original() == original, "repeated start preserves the first original snapshot");
        store.save_report({c.lcd, 1});
        check(kafel::json::parse(read_text(fs.path / "state/status.json")).get("mode") ==
                  kafel::json::Json("hdmi"),
              "atomic status JSON remains readable by the audit");
        struct stat st{};
        stat((fs.path / "state/original.json").c_str(), &st);
        check((st.st_mode & 0777) == 0600, "state files remain private");
        for (const auto &entry : std::filesystem::directory_iterator(fs.path / "state"))
            check(!entry.path().filename().string().starts_with('.'),
                  "atomic writes leave no temporary files");
        store.clear();
        rejects([&] { (void)store.original(); }, "No original");
        std::filesystem::create_symlink(fs.path / "outside", fs.path / "state/status.json");
        rejects([&] { store.save_report({c.lcd, 1}); });
        check(!std::filesystem::exists(fs.path / "outside"), "state symlink cannot overwrite another file");
    }
    {
        Temporary fs;
        std::filesystem::create_directory(fs.path / "unsafe");
        chmod((fs.path / "unsafe").c_str(), 0755);
        rejects([&] { StateStore store(fs.path / "unsafe"); });
        std::filesystem::create_directory_symlink(fs.path / "unsafe", fs.path / "alias");
        rejects([&] { StateStore store(fs.path / "alias"); });
        FakeConsole c;
        auto state = kafel::json::parse(encode(snapshot(c)));
        state.get<kafel::json::Object>()["lcd"] = kafel::json::Json("/etc/passwd");
        rejects([&] { (void)decode_snapshot(state.serialize()); });
    }
    {
        Temporary fs;
        FakeConsole c(1);
        const auto before = snapshot(c);
        StateStore store(fs.path / "state");
        std::filesystem::create_symlink(fs.path / "outside", fs.path / "state/status.json");
        rejects([&] { (void)isolate_and_record(c, store); });
        check(snapshot(c) == before && store.original() == before,
              "status persistence failure restores console routing and preserves the original snapshot");
        std::filesystem::remove(fs.path / "state/status.json");
        chmod((fs.path / "state/original.json").c_str(), 0644);
        rejects([&] { (void)store.original(); }, "Unsafe");
        chmod((fs.path / "state/original.json").c_str(), 0600);
        std::filesystem::create_hard_link(fs.path / "state/original.json", fs.path / "shared");
        rejects([&] { (void)store.original(); }, "Unsafe");
    }
    {
        Temporary fs;
        std::filesystem::create_directory(fs.path / "state");
        chmod((fs.path / "state").c_str(), 0700);
        put(fs.path / "outside", "keep");
        std::filesystem::create_symlink(fs.path / "outside", fs.path / "state/lock");
        rejects([&] { StateStore store(fs.path / "state"); });
        check(read_text(fs.path / "outside") == "keep", "lock symlink cannot modify unrelated files");
        FakeConsole c;
        auto value = kafel::json::parse(encode(snapshot(c)));
        value.get<kafel::json::Object>()["maps"] =
            kafel::json::Json(kafel::json::Array{kafel::json::Json(7.)});
        rejects([&] { (void)decode_snapshot(value.serialize()); });
        rejects([&] { (void)decode_snapshot("{\"lcd\":\"/dev/fb7\",\"lcd\":\"/dev/fb1\"}"); });
    }
    std::puts("PASS native console: HDMI/detach, all VTs, readback, rollback, DRM discovery, bounded JSON, "
              "private atomic state and locking");
}
