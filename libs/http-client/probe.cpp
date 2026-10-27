#include "net/http_client.hpp"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <sys/resource.h>
namespace {
void memory() {
#ifdef __linux__
    if (auto *file = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, file))
            if (std::string_view(line).starts_with("VmRSS:") || std::string_view(line).starts_with("VmHWM:"))
                std::printf("%s", line);
        std::fclose(file);
    }
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::printf("peak_rss_kib=%ld\n", usage.ru_maxrss / 1024);
#endif
}
bool number(const char *input, std::size_t &output) {
    const std::string_view value(input);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: http-client-probe URL CA_FILE TIMEOUT_MS BODY_LIMIT\n");
        return 2;
    }
    std::size_t timeout = 0, limit = 0;
    if (!number(argv[3], timeout) || timeout > 86400000 || !number(argv[4], limit))
        return 2;
    net::HttpClient client(argv[2]);
    const auto start = std::chrono::steady_clock::now();
    const auto result = client.get(argv[1], {std::chrono::milliseconds(timeout), limit});
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();
    std::printf("elapsed_ms=%lld\n", static_cast<long long>(elapsed));
    memory();
    if (!result) {
        const auto name = net::error_name(result.error().code);
        std::printf("ERROR code=%.*s message=%s\n", static_cast<int>(name.size()), name.data(),
                    result.error().message.c_str());
        return 1;
    }
    std::printf("HTTP status=%d body_bytes=%zu\n", result->status, result->body.size());
    if (result->body.size() <= 8192)
        std::printf("BODY %s\n", result->body.c_str());
    return 0;
}
