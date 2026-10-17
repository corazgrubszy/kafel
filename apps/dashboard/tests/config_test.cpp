#include "app/config.hpp"
#include "platform/linux/display_format.hpp"
#include "ui/ribbon.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
void check(bool,const char*);
namespace {
const char* environment(const char* key) {
    const std::string_view k=key;
    if(k=="KAFEL_FRAMEBUFFER") return "/dev/fb7";
    if(k=="KAFEL_TOUCH") return "off";
    if(k=="KAFEL_IDLE_TIMEOUT") return "12";
    return nullptr;
}
}
void test_config() {
    using namespace kafel::app;
    const auto defaults=parse_config({});
    check(defaults.framebuffer=="auto"&&defaults.touch=="auto"&&defaults.idle_timeout_ms==60000,"device defaults");
    const auto env=parse_config({},environment);
    check(env.framebuffer=="/dev/fb7"&&env.touch=="off"&&env.idle_timeout_ms==12000,"environment configuration");
    const std::array<std::string_view,4> args={"--fb=/dev/fb2","--touch=/dev/input/event8","--idle-timeout=0","--check"};
    const auto cli=parse_config(args,environment);
    check(cli.framebuffer=="/dev/fb2"&&cli.touch=="/dev/input/event8"&&cli.idle_timeout_ms==0&&cli.command==Command::Check,"CLI overrides environment for checks");
    for(const auto bad:{"--fb=","--fb=relative","--touch=none","--idle-timeout=-1","--idle-timeout=86401","--idle-timeout=2x","--socket=/tmp/","--unknown"}) {
        bool rejected=false;try {const std::array<std::string_view,1> a={bad};(void)parse_config(a);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"reject invalid CLI");
    }
    using kafel::linux_device::DisplayFormat;
    DisplayFormat lcd; lcd.path="/dev/fb7";lcd.driver="fb_st7789v";lcd.width=240;lcd.height=320;lcd.bpp=16;lcd.visual=2;
    lcd.stride=512;lcd.memory=512*320;lcd.nonstd=1;lcd.red_offset=11;lcd.red_length=5;lcd.green_offset=5;lcd.green_length=6;lcd.blue_length=5;
    auto hdmi=lcd;hdmi.path="/dev/fb0";hdmi.width=640;
    const std::array displays={hdmi,lcd};
    check(kafel::linux_device::unique_display(displays)=="/dev/fb7","select LCD by format, not index");
    const std::array ambiguous={lcd,lcd};bool failed=false;
    try {(void)kafel::linux_device::unique_display(ambiguous);}catch(const std::runtime_error&){failed=true;}
    check(failed,"ambiguous LCD requires explicit selection");
    auto bad=lcd;bad.memory--;check(!bad.supported(),"framebuffer bounds validated");
    bad=lcd;bad.driver="unknown";check(!bad.supported(),"unknown nonstd rejected");
    bad=lcd;bad.green_offset=6;check(!bad.supported(),"wrong RGB layout rejected");
    ui::RibbonLayout layout;const std::array<unsigned,4> spans={1,1,1,1};check(layout.configure({4,240,232,76},spans),"idle layout");
    ui::RibbonController ribbon(12000);ribbon.configure(layout,0);ribbon.select(3,100,false);
    check(ribbon.idle_deadline()==12100,"custom idle deadline");ribbon.update(12100);check(ribbon.selected()==0,"custom idle return");
    ui::RibbonController fixed(0);fixed.configure(layout,0);fixed.select(3,100,false);fixed.update(999999);
    check(!fixed.idle_deadline()&&fixed.selected()==3,"zero disables idle return");
    std::puts("PASS dashboard configuration, device selection, RGB565 contract and configurable idle");
}
