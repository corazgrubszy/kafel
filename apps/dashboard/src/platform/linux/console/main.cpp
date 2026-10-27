#include "kafel_build_info.h"
#include "system.hpp"
#include <cstdio>
#include <unistd.h>
int main(int argc, char **argv) {
    using namespace kafel::console;
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::printf("kafel-display-console 0.5.0-dev git=%s\n", kafel_git_revision());
            return 0;
        }
        const bool isolate_action = argc >= 2 && std::string_view(argv[1]) == "isolate";
        const bool restore_action = argc >= 2 && std::string_view(argv[1]) == "restore";
        if ((!isolate_action && !restore_action) || argc != 2) {
            std::fputs("usage: kafel-display-console isolate|restore\n", stderr);
            return 2;
        }
        if (geteuid() != 0)
            throw std::runtime_error("Root required for console routing");
        StateStore store("/run/kafel-display");
        if (restore_action) {
            const auto original = store.original();
            LinuxConsole console(original.lcd);
            restore(console, original);
            store.clear();
            std::puts("display: original console routing restored");
        } else {
            LinuxConsole console(detect_lcd());
            const auto report = isolate_and_record(console, store);
            std::printf("display: %s", encode(report).c_str());
        }
    } catch (const std::exception &failure) {
        std::fprintf(stderr, "display isolation failed: %s\n", failure.what());
        return 1;
    }
}
