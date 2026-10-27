#include "isolation.hpp"
#include "json/strict.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
namespace kafel::console {
namespace {
[[noreturn]] void fail(const char *reason) { throw std::runtime_error(reason); }
bool digits(std::string_view text) {
    return !text.empty() && text.find_first_not_of("0123456789") == text.npos;
}
bool attached(const Bindings &bindings) {
    return std::any_of(bindings.begin(), bindings.end(), [](const auto &entry) { return entry.second; });
}
void validate(const Snapshot &state) {
    (void)framebuffer_index(state.lcd);
    if (state.bindings.size() > 64 || (!state.maps.empty() && state.maps.size() != console_count) ||
        (attached(state.bindings) && state.maps.size() != console_count))
        fail("Invalid console snapshot");
    for (const auto &[name, bound] : state.bindings) {
        (void)bound;
        if (!console_name(name))
            fail("Invalid console name");
    }
}
} // namespace
unsigned framebuffer_index(std::string_view path) {
    if (!path.starts_with("/dev/fb"))
        fail("Invalid LCD framebuffer path");
    auto number = path.substr(7);
    unsigned index = 0;
    auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
    if (!digits(number) || parsed.ec != std::errc{} || index >= framebuffer_count)
        fail("Invalid LCD framebuffer index");
    return index;
}
bool console_name(std::string_view name) noexcept {
    return name.starts_with("vtcon") && digits(name.substr(5));
}
Console::Console(std::string path) : lcd(std::move(path)) { (void)framebuffer_index(lcd); }
Snapshot snapshot(Console &console) {
    auto bindings = console.bindings();
    auto maps = attached(bindings) ? console.maps() : Mapping{};
    Snapshot state{console.lcd, std::move(bindings), std::move(maps)};
    validate(state);
    return state;
}
void restore(Console &console, const Snapshot &state) {
    validate(state);
    if (state.lcd != console.lcd)
        fail("Snapshot belongs to another LCD");
    std::string errors;
    auto attempt = [&](auto operation) {
        try {
            operation();
        } catch (const std::exception &error) {
            errors += std::string(error.what()) + "; ";
        }
    };
    // Restore mappings before allowing text rendering again. -1/unmapped is
    // readable through fbdev, but cannot be passed back as a target.
    for (unsigned i = 0; i < state.maps.size(); ++i)
        if (state.maps[i] < framebuffer_count)
            attempt([&] { console.map(i + 1, state.maps[i]); });
    for (const auto &[name, bound] : state.bindings)
        attempt([&] { console.bind(name, bound); });
    attempt([&] {
        const auto observed = console.bindings();
        for (const auto &[name, bound] : state.bindings)
            if (!observed.contains(name) || observed.at(name) != bound)
                fail("Console restore binding verification failed");
        if (attached(state.bindings)) {
            const auto mapping = console.maps();
            if (mapping.size() != state.maps.size())
                fail("Console restore mapping verification failed");
            for (std::size_t i = 0; i < state.maps.size(); ++i)
                if (state.maps[i] < framebuffer_count && mapping[i] != state.maps[i])
                    fail("Console restore mapping verification failed");
        }
    });
    if (!errors.empty())
        throw std::runtime_error("Console restore failed: " + errors);
}
Report isolate(Console &console, const Snapshot &before) {
    validate(before);
    if (before.lcd != console.lcd)
        fail("Snapshot belongs to another LCD");
    try {
        auto target = console.hdmi();
        if (target && (*target == framebuffer_index(console.lcd) || *target >= framebuffer_count))
            fail("Console target equals LCD or is outside fbdev limits");
        if (target && attached(before.bindings) &&
            std::all_of(before.maps.begin(), before.maps.end(),
                        [](auto fb) { return fb < framebuffer_count; })) {
            for (unsigned tty = 1; tty <= console_count; ++tty)
                console.map(tty, *target);
            if (console.maps() != Mapping(console_count, *target))
                fail("HDMI mapping verification failed");
            return {console.lcd, target};
        }
        for (const auto &[name, bound] : before.bindings)
            if (bound)
                console.bind(name, false);
        if (attached(console.bindings()))
            fail("fbcon is still attached");
        return {console.lcd, std::nullopt};
    } catch (const std::exception &error) {
        const std::string primary = error.what();
        try {
            restore(console, before);
        } catch (const std::exception &rollback) {
            throw std::runtime_error(primary + "; " + rollback.what());
        }
        throw;
    }
}
std::string read_text(const std::filesystem::path &path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), "Read console metadata");
    try {
        std::string text;
        char buffer[1024];
        for (;;) {
            auto n = read(fd, buffer, sizeof buffer);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                throw std::system_error(errno, std::generic_category(), "Read console metadata");
            if (!n)
                break;
            text.append(buffer, static_cast<std::size_t>(n));
            if (text.size() > 65536)
                fail("Oversized console metadata");
        }
        close(fd);
        return text;
    } catch (...) {
        close(fd);
        throw;
    }
}
namespace {
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace
Bindings framebuffer_bindings(const std::filesystem::path &root) {
    Bindings result;
    for (const auto &entry : std::filesystem::directory_iterator(root / "sys/class/vtconsole")) {
        const auto name = entry.path().filename().string();
        if (!console_name(name) || read_text(entry.path() / "name").find("frame buffer") == std::string::npos)
            continue;
        const auto bytes = read_text(entry.path() / "bind");
        const auto value = trim(bytes);
        if (value != "0" && value != "1")
            fail("Invalid console binding");
        result.emplace(name, value == "1");
    }
    return result;
}
std::optional<unsigned> find_hdmi(const std::filesystem::path &root, std::string_view lcd) {
    (void)framebuffer_index(lcd);
    const auto bytes = read_text(root / "proc/cmdline");
    std::string_view cmdline = bytes;
    bool forced = false;
    while (!(cmdline = trim(cmdline)).empty()) {
        const auto end = cmdline.find_first_of(" \t\r\n");
        const auto token = cmdline.substr(0, end);
        cmdline.remove_prefix(end == cmdline.npos ? cmdline.size() : end);
        if (token.starts_with("video=HDMI-A-1:")) {
            if (forced || token != hdmi_mode)
                fail("Conflicting HDMI mode in running kernel");
            forced = true;
        }
    }
    if (!forced)
        return {};
    std::optional<unsigned> result;
    for (const auto &fb : std::filesystem::directory_iterator(root / "sys/class/graphics")) {
        const auto name = fb.path().filename().string();
        if (!name.starts_with("fb") || !digits(std::string_view(name).substr(2)) || "/dev/" + name == lcd)
            continue;
        const auto driver = read_text(fb.path() / "name");
        if (trim(driver) != "vc4drmfb")
            continue;
        const auto device = std::filesystem::weakly_canonical(fb.path() / "device");
        for (const auto &card : std::filesystem::directory_iterator(root / "sys/class/drm")) {
            const auto id = card.path().filename().string();
            if (!id.starts_with("card") || !digits(std::string_view(id).substr(4)))
                continue;
            if (device != std::filesystem::weakly_canonical(card.path() / "device") &&
                device != std::filesystem::weakly_canonical(card.path()))
                continue;
            if (!std::filesystem::exists(card.path().parent_path() / (id + "-HDMI-A-1")))
                continue;
            const auto index = framebuffer_index("/dev/" + name);
            if (result && *result != index)
                fail("Ambiguous HDMI framebuffer");
            result = index;
        }
    }
    return result;
}
std::string encode(const Snapshot &state) {
    validate(state);
    json::Object bindings;
    for (const auto &[name, bound] : state.bindings)
        bindings[name] = json::Json(bound ? 1. : 0.);
    json::Array maps;
    for (auto fb : state.maps)
        maps.emplace_back(double(fb));
    return json::Json(json::Object{{"lcd", json::Json(state.lcd)},
                                   {"bindings", json::Json(bindings)},
                                   {"maps", json::Json(maps)}})
               .serialize() +
           "\n";
}
Snapshot decode_snapshot(std::string_view bytes) {
    const auto root = json::parse(bytes);
    json::shape(root, {"lcd", "bindings", "maps"});
    Snapshot state;
    state.lcd = json::text(root.get("lcd"), 32);
    if (!root.get("bindings").is<json::Object>() || root.get("bindings").get<json::Object>().size() > 64 ||
        !root.get("maps").is<json::Array>() || root.get("maps").get<json::Array>().size() > console_count)
        fail("Invalid console snapshot");
    for (const auto &[name, bound] : root.get("bindings").get<json::Object>())
        state.bindings[name] = json::integer(bound, 0, 1) == 1;
    for (const auto &fb : root.get("maps").get<json::Array>())
        state.maps.push_back(static_cast<std::uint32_t>(json::integer(fb, 0, UINT32_MAX)));
    validate(state);
    return state;
}
std::string encode(const Report &report) {
    (void)framebuffer_index(report.lcd);
    if (report.hdmi && (*report.hdmi >= framebuffer_count || *report.hdmi == framebuffer_index(report.lcd)))
        fail("Invalid console report");
    return json::Json(
               json::Object{{"mode", json::Json(report.hdmi ? "hdmi" : "detached")},
                            {"lcd", json::Json(report.lcd)},
                            {"console", report.hdmi ? json::Json("/dev/fb" + std::to_string(*report.hdmi))
                                                    : json::Json()}})
               .serialize() +
           "\n";
}
} // namespace kafel::console
