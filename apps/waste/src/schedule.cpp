#include "schedule.hpp"
#include "json/document.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kafel::waste {
namespace {
constexpr std::array names={"mixed","glass","paper","plastic","kitchen","garden"};
bool zone_name(std::string_view zone) {
    if(zone.empty()||zone.size()>128||zone.front()=='/'||zone.find("..")!=zone.npos)return false;
    for(unsigned char c:zone)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-'||c=='+'))return false;
    return true;
}
}
std::optional<Date> parse_date(std::string_view text) noexcept {
    if(text.size()!=10||text[4]!='-'||text[7]!='-')return {};
    int year=0;unsigned month=0,day=0;
    auto number=[](std::string_view input,auto& value) {
        for(char c:input)if(c<'0'||c>'9')return false;
        const auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);
        return parsed.ec==std::errc{}&&parsed.ptr==input.data()+input.size();
    };
    if(!number(text.substr(0,4),year)||!number(text.substr(5,2),month)||!number(text.substr(8,2),day)||year<1900)return {};
    const std::chrono::year_month_day date{std::chrono::year(year),std::chrono::month(month),std::chrono::day(day)};
    if(!date.ok())return {};
    return Date(date);
}
ScheduleResult parse_schedule(std::string_view text) {
    const auto document=json::document(text,maximum_schedule_bytes);
    if(!document||!document->is<picojson::object>())return Error{"Invalid schedule JSON"};
    const auto& zone=document->get("timezone");const auto& pickups=document->get("pickups");
    if(!zone.is<std::string>()||!zone_name(zone.get<std::string>())||!pickups.is<picojson::array>())
        return Error{"Expected timezone and pickups array"};
    const auto& entries=pickups.get<picojson::array>();
    if(entries.size()>4096)return Error{"Too many pickups"};
    Schedule schedule{zone.get<std::string>(),{}};
    schedule.days.reserve(entries.size());
    for(std::size_t index=0;index<entries.size();++index) {
        const auto& entry=entries[index];
        if(!entry.is<picojson::object>()||!entry.get("date").is<std::string>()||!entry.get("fraction").is<std::string>())
            return Error{"Expected pickup date and fraction"};
        const auto date=parse_date(entry.get("date").get<std::string>());
        const auto& name=entry.get("fraction").get<std::string>();
        const auto fraction=std::find(names.begin(),names.end(),name);
        if(!date)return Error{"Invalid date at pickup "+std::to_string(index)};
        if(fraction==names.end())return Error{"Unknown fraction at pickup "+std::to_string(index)};
        schedule.days.push_back({*date,1U<<static_cast<unsigned>(fraction-names.begin())});
    }
    std::sort(schedule.days.begin(),schedule.days.end(),[](auto a,auto b){return a.date<b.date;});
    std::size_t used=0;
    for(auto day:schedule.days) {
        if(used&&schedule.days[used-1].date==day.date)schedule.days[used-1].fractions|=day.fractions;
        else schedule.days[used++]=day;
    }
    schedule.days.resize(used);
    return schedule;
}
ScheduleResult load_schedule(const char* path) {
    const int fd=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return Error{"Cannot open schedule"};
    struct Close { int fd;~Close(){close(fd);} } close_file{fd};
    struct stat metadata{};
    if(fstat(fd,&metadata)<0||!S_ISREG(metadata.st_mode)||metadata.st_size<0||
       static_cast<std::uint64_t>(metadata.st_size)>maximum_schedule_bytes)return Error{"Schedule must be a bounded regular file"};
    std::string bytes;bytes.reserve(static_cast<std::size_t>(metadata.st_size));
    std::array<char,4096> buffer{};
    for(;;) {
        const auto size=read(fd,buffer.data(),buffer.size());
        if(size<0) {if(errno==EINTR)continue;return Error{"Cannot read schedule"};}
        if(size==0)break;
        if(bytes.size()+static_cast<std::size_t>(size)>maximum_schedule_bytes)return Error{"Schedule exceeds byte limit"};
        bytes.append(buffer.data(),static_cast<std::size_t>(size));
    }
    return parse_schedule(bytes);
}
bool activate_timezone(std::string_view zone) {
    if(!zone_name(zone))return false;
    const std::string name(zone);
    std::error_code error;
    if(!std::filesystem::is_regular_file(std::filesystem::path("/usr/share/zoneinfo")/name,error))return false;
    if(setenv("TZ",name.c_str(),1)!=0)return false;
    tzset();return true;
}
std::optional<Date> local_date(std::time_t now) noexcept {
    std::tm local{};if(!localtime_r(&now,&local))return {};
    const auto date=std::chrono::year_month_day{std::chrono::year(local.tm_year+1900),
        std::chrono::month(static_cast<unsigned>(local.tm_mon+1)),std::chrono::day(static_cast<unsigned>(local.tm_mday))};
    if(!date.ok())return {};return Date(date);
}
std::optional<Error> ScheduleCache::reload(const char* path) {
    auto result=load_schedule(path);
    if(auto* error=std::get_if<Error>(&result)) { failed_=true;return *error; }
    auto& schedule=std::get<Schedule>(result);
    if(!activate_timezone(schedule.timezone)) { failed_=true;return Error{"Timezone unavailable"}; }
    schedule_=std::move(schedule);failed_=false;return {};
}
std::optional<Selection> select(const Schedule& schedule,Date today,std::int64_t seconds) noexcept {
    const auto first=std::lower_bound(schedule.days.begin(),schedule.days.end(),today,[](auto day,auto date){return day.date<date;});
    if(first==schedule.days.end())return {};
    const auto tomorrow=today+std::chrono::days(1);
    if(first->date==today) {
        const auto next=first+1;
        if(next!=schedule.days.end()&&next->date==tomorrow&&((seconds/5)%2+2)%2==1)
            return Selection{*next,Selection::When::Tomorrow,true};
        return Selection{*first,Selection::When::Today,true};
    }
    return Selection{*first,first->date==tomorrow?Selection::When::Tomorrow:Selection::When::Next,first->date==tomorrow};
}
}
