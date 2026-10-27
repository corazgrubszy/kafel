#include "system/system_metrics.hpp"
#include <cstdio>
#include <limits>
void check(bool, const char *);
void test_system_metrics() {
    using namespace kafel;
    auto ticks = parse_cpu_ticks("cpu  100 20 30 400 10 5 6 7 80 9\ncpu0 1 2\n");
    check(ticks && ticks->total == 578 && ticks->idle == 410, "CPU parser excludes duplicated guest time");
    check(!parse_cpu_ticks("cpu0 1 2 3 4 5 6 7 8") && !parse_cpu_ticks("cpu 1 2"),
          "bad CPU records rejected");
    check(!parse_cpu_ticks("cpu 18446744073709551615 1 0 0 0 0 0 0"), "CPU sum overflow rejected");
    check(cpu_usage({100, 40}, {200, 115}) == 25 && cpu_usage({100, 40}, {200, 40}) == 100,
          "CPU uses interval rather than lifetime average");
    check(!cpu_usage({200, 100}, {100, 40}) && !cpu_usage({100, 100}, {200, 90}) &&
              !cpu_usage({100, 0}, {110, 20}),
          "counter reset/iowait decrease cannot fake CPU load");
    check(parse_ram_usage("MemTotal: 1000 kB\nMemFree: 2 kB\nMemAvailable: 620 kB\n") == 38,
          "RAM uses available memory including reclaimable cache");
    check(!parse_ram_usage("MemTotal: 0 kB\nMemAvailable: 0 kB\n") &&
              !parse_ram_usage("MemTotal: 10 kB\nMemAvailable: 20 kB\n"),
          "invalid memory readings stay unknown");
    check(!parse_ram_usage("MemTotal: 100 kB\nMemFree: 20 kB\n"),
          "missing MemAvailable is not replaced with inaccurate MemFree");
    check(parse_temperature("48000\n") == 48000 && !parse_temperature("") &&
              !parse_temperature("48000junk") && !parse_temperature("900000"),
          "temperature validation");
    SystemSnapshot snapshot;
    SystemDisplay display;
    display.update(snapshot);
    check(display.cpu.view() == "--%" && display.free_space.view() == "-- GB",
          "missing metrics do not become zero");
    snapshot.cpu_percent = 0;
    snapshot.ram_percent = 100;
    snapshot.storage_free_bytes = 18600000000ULL;
    display.update(snapshot);
    check(display.cpu.view() == "0%" && display.ram.view() == "100%" &&
              display.free_space.view() == "18,6 GB",
          "zero, 100 percent and deterministic decimal GB formatting");
    snapshot.storage_free_bytes = UINT64_MAX;
    display.update(snapshot);
    check(display.free_space.view().ends_with(" TB"), "large storage counts format without overflow");
    const auto route = default_ipv4_interface("Iface Destination Gateway Flags RefCnt Use Metric Mask\n"
                                              "eth0 00000000 0100000A 0003 0 0 600 00000000\n"
                                              "wlan0 00000000 0100000A 0003 0 0 100 00000000\n"
                                              "tun0 00000000 00000000 0001 0 0 0 80000000\n");
    check(route.view() == "wlan0" && default_ipv4_interface("bad table").view().empty(),
          "IP prefers lowest-metric real default route, not a split route");
    FixedText<4> fixed;
    fixed.assign("abcdef");
    check(fixed.view() == "abc", "fixed buffers always terminate");
    fixed.assign(fixed.view());
    check(fixed.view() == "abc", "fixed buffers support self assignment");
    std::puts("PASS: proc parsing, CPU deltas, memory availability, missing readings and formatting");
}
