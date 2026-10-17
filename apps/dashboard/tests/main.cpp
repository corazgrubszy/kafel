#include <cstdio>
#include <cstdlib>
void check(bool condition,const char* message) { if(!condition) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
void test_config();
void test_readable_typography();
void test_dashboard();
void test_frame_schedule();
void test_services();
void test_dash4();
void test_metric_tiles();
void test_meter_tiles();
void test_waste_tile();
void test_surface();
void test_system_metrics();
void test_touch();
void test_marquee();
void test_allocations();
int main() {
    test_config();
    test_readable_typography();
    test_dashboard();
    test_frame_schedule();
    test_services();
    test_dash4();
    test_metric_tiles();
    test_meter_tiles();
    test_waste_tile();
    test_surface();
    test_system_metrics();
    test_touch();
    test_marquee();
    test_allocations();
}
