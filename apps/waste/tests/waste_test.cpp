#include "schedule.hpp"
#include "packet.hpp"
#include "services/service_registry.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
using namespace kafel::waste;
void check(bool condition,const char* label) {
    if(!condition){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}
}
const std::string sample=R"({"timezone":"Europe/Warsaw","pickups":[{"date":"2026-10-19","fraction":"paper"},{"date":"2026-10-18","fraction":"glass"},{"date":"2026-10-17","fraction":"plastic"},{"date":"2026-10-17","fraction":"mixed"},{"date":"2026-10-17","fraction":"plastic"}]})";
std::time_t utc(int year,int month,int day,int hour,int minute) {
    const auto date=std::chrono::sys_days(std::chrono::year(year)/month/day);
    return std::chrono::duration_cast<std::chrono::seconds>(date.time_since_epoch()).count()+hour*3600+minute*60;
}
int main() {
    auto result=parse_schedule(sample);
    check(std::holds_alternative<Schedule>(result),"valid JSON");
    const auto schedule=std::get<Schedule>(result);
    check(schedule.days.size()==3&&schedule.days[0].fractions==9,"dates sorted, fractions deduplicated and grouped");
    const auto today=*parse_date("2026-10-17");
    const auto a=select(schedule,today,0),b=select(schedule,today,5);
    check(a&&b&&a->when==Selection::When::Today&&b->when==Selection::When::Tomorrow&&a->attention&&b->attention,"today/tomorrow rotate atomically");
    auto packet=dashboard_packet(&schedule,today,0);
    check(packet=="DASH/4\tkafel-waste\tKafel Waste\nTILE\twaste\t1\tok\tattention\tŚmieci\ttext\t[#B8C0C8]🗑[/][#E6B800]🗑[/]\u2028Dzisiaj\n","one-column golden packet, stable palette, compact bins");
    const auto bio=parse_schedule(R"({"timezone":"UTC","pickups":[{"date":"2026-10-17","fraction":"kitchen"},{"date":"2026-10-17","fraction":"garden"}]})");
    check(std::holds_alternative<Schedule>(bio),"both bio fractions accepted");
    const auto bio_packet=dashboard_packet(&std::get<Schedule>(bio),today,0);
    check(bio_packet.find("[#A0522D]🗑[/][#008F39]🗑[/]\u2028Dzisiaj")!=bio_packet.npos,
          "kitchen is brown and garden uses the same green as glass");
    const auto parsed=kafel::parse_service_message(packet);
    check(parsed&&parsed->tiles[0].span==1&&parsed->tiles[0].attention,"real DASH/4 parser accepts waste snapshot");
    packet=dashboard_packet(&schedule,today,5);
    check(packet.find("[#008F39]🗑[/]\u2028Jutro")!=packet.npos&&packet.find("Dzisiaj")==packet.npos,"icons and caption change together");
    packet=dashboard_packet(&schedule,*parse_date("2026-10-15"),0);
    check(packet.find("normal\tŚmieci")!=packet.npos&&packet.find("17 października")!=packet.npos,"nearest pickup includes all fractions and full Polish month");
    check(dashboard_packet(&schedule,today,0,true).find("warning\tattention")!=std::string::npos,"failed reload retains data with warning and attention");
    check(dashboard_packet(nullptr,today,0).find("error\tnormal")!=std::string::npos,"missing schedule error");
    check(dashboard_packet(&schedule,*parse_date("2027-01-01"),0).find("warning\tnormal\tŚmieci\ttext\tBrak terminów")!=std::string::npos,"expired annual schedule is visible warning");
    check(kafel::parse_service_message(dashboard_packet(&schedule,today,0,false,2))->tiles[0].span==2,"two-column configuration");
    for(auto bad:{"2026-02-29","2026-13-01","2026-10-32","2026-1-01","0000-01-01","2026-01-01x"})check(!parse_date(bad),"invalid civil dates rejected");
    check(parse_date("2028-02-29").has_value(),"leap day accepted");
    for(const auto& bad:{std::string("{}"),sample+"{}",std::string("{\"timezone\":\"../UTC\",\"pickups\":[]}"),
        std::string("{\"timezone\":\"UTC\",\"pickups\":[{\"date\":\"2026-02-29\",\"fraction\":\"paper\"}]}"),
        std::string("{\"timezone\":\"UTC\",\"pickups\":[{\"date\":\"2026-10-17\",\"fraction\":\"unknown\"}]}"),
        std::string("{\"timezone\":\"UTC\",\"pickups\":[null]}"),std::string(33,'[')+"0"+std::string(33,']'),
        std::string(maximum_schedule_bytes+1,' ')})
        check(std::holds_alternative<Error>(parse_schedule(bad)),"malformed schedule rejected atomically");
    check(activate_timezone("Europe/Warsaw"),"timezone database available");
    check(local_date(utc(2026,3,29,22,30))==parse_date("2026-03-30"),"spring DST local date boundary");
    check(local_date(utc(2026,10,25,23,30))==parse_date("2026-10-26"),"autumn DST local date boundary");
    check(local_date(utc(2026,12,31,23,30))==parse_date("2027-01-01"),"local new year");
    check(!activate_timezone("../etc/passwd")&&!activate_timezone("Missing/Zone"),"timezone traversal and missing zone rejected");
    const auto boundary=parse_schedule(R"({"timezone":"Europe/Warsaw","pickups":[{"date":"2027-01-01","fraction":"paper"}]})");
    check(select(std::get<Schedule>(boundary),*parse_date("2026-12-31"),0)->when==Selection::When::Tomorrow,"tomorrow across year boundary");
    const auto leap=parse_schedule(R"({"timezone":"UTC","pickups":[{"date":"2028-02-29","fraction":"paper"}]})");
    check(select(std::get<Schedule>(leap),*parse_date("2028-02-28"),0)->when==Selection::When::Tomorrow,"tomorrow across leap boundary");
    char path[]="/tmp/kafel-waste-test.XXXXXX";const int fd=mkstemp(path);check(fd>=0,"temporary fixture");close(fd);
    {std::ofstream file(path);file<<sample;}
    check(std::holds_alternative<Schedule>(load_schedule(path)),"real bounded file load");
    ScheduleCache cache;
    check(!cache.reload(path)&&cache.get()&&!cache.degraded(),"cache accepts complete schedule");
    const auto count=cache.get()->days.size();
    {std::ofstream file(path);file<<"{";}
    check(cache.reload(path).has_value()&&cache.get()->days.size()==count&&cache.degraded(),"failed reload retains previous complete schedule");
    check(dashboard_packet(cache.get(),today,0,cache.degraded()).find("warning\tattention")!=std::string::npos,"reload failure produces warning without losing pickup attention");
    {std::ofstream file(path);file<<sample;}
    check(!cache.reload(path)&&!cache.degraded(),"cache recovers after repaired file");
    {std::ofstream file(path);file<<"{";}
    check(std::holds_alternative<Error>(load_schedule(path)),"truncated file rejected");
    unlink(path);check(std::holds_alternative<Error>(load_schedule(path)),"missing file rejected");
    check(std::holds_alternative<Error>(load_schedule("/tmp")),"non-regular input rejected");
    std::puts("PASS waste schedule, validation, grouping, calendar/DST, attention, one-column DASH/4 and file errors");
}
