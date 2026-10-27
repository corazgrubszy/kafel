#include <cstdio>
#include <cstdlib>
void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void test_async_compiler();
void test_config();
void test_console();
void test_readable_typography();
void test_dashboard();
void test_dash5();
void test_cards();
void test_unix_protocol();
void test_allocations();
void test_frame_schedule();
void test_surface();
void test_system_metrics();
void test_touch();
void test_marquee();
int main() {
    test_async_compiler();
    test_config();
    test_console();
    test_readable_typography();
    test_dashboard();
    test_dash5();
    test_cards();
    test_unix_protocol();
    test_allocations();
    test_frame_schedule();
    test_surface();
    test_system_metrics();
    test_touch();
    test_marquee();
}
