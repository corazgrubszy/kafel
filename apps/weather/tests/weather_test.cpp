#include "weather.hpp"
#include "packet.hpp"
#include "platform/unix_publisher.hpp"
#include "services/service_registry.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

using namespace kafel::weather;
    using namespace kafel::unix_transport;
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
std::string packet(const WeatherCache& cache, Clock::time_point now) {
    return dashboard_packet(cache.snapshot(now));
}
const std::string valid = R"({"current_weather":{"temperature":12.8,"windspeed":18,"time":1800000900},"hourly":{"time":[1800000000,1800003600],"rain":[0,1],"snowfall":[0,0]}})";
std::string replace(std::string input, std::string_view from, std::string_view to) {
    const auto position = input.find(from);
    check(position != input.npos, "test replacement exists");
    input.replace(position, from.size(), to);
    return input;
}
int main() {
    const auto conditions = decode_forecast(valid);
    check(conditions && conditions->temperature == 12.8 && conditions->wind_kmh == 18 &&
        conditions->kind == Conditions::Kind::Clear, "current hour, not future rain");
    check(decode_forecast(replace(valid, "[0,1]", "[1,1]"))->kind == Conditions::Kind::Rain, "rain");
    check(decode_forecast(replace(replace(valid, "[0,1]", "[1,1]"), "[0,0]", "[1,0]"))->kind == Conditions::Kind::Snow, "snow precedence");
    for (const auto& bad : {std::string("{}"), valid + "{}", replace(valid,"12.8","null"),
        replace(valid,"12.8","\"12.8\""), replace(valid,"12.8","101"), replace(valid,"12.8","1e999"),
        replace(valid,"\"windspeed\":18","\"windspeed\":-1"), replace(valid,"[0,1]","[0]"),
        replace(valid,"[0,1]","[0,null]"), replace(valid,"[0,0]","[0,-1]"),
        replace(valid,"1800003600","1800000000"), replace(valid,"1800003600","1800007200"),
        replace(valid,"1800000900","1800000900.5"), replace(valid,"1800000900","1700000000"),
        replace(valid,"1800000900","1900000000"), std::string(33,'[') + "0" + std::string(33,']'),
        std::string(65537, ' '), valid.substr(0, valid.size()-1)})
        check(!decode_forecast(bad), "reject malformed/incomplete/out-of-range forecast");
    check(decode_forecast(valid+" \n\t").has_value(), "trailing whitespace");
    check(forecast_url("52.123456", "17").has_value(), "coordinates");
    check(forecast_url("-90", "180").has_value(), "coordinate boundaries");
    for (auto bad : {"", "nan", "inf", "91", "52&evil=1", "52 ", "1e999"})
        check(!forecast_url(bad,"17"), "reject bad latitude");
    check(!forecast_url("52","181"), "longitude range");
    check(forecast_url("52", "17")->find("timeformat=unixtime&timezone=GMT") != std::string::npos, "UTC epoch request");
    check(!valid_socket_path({}) && !valid_socket_path(std::string_view("a\0b", 3)), "invalid socket paths");
    RetrySchedule retry;
    for (int expected : {30,60,120,300,300,300}) check(retry.next(false).count()==expected,"retry saturation");
    check(retry.next(true).count()==900 && retry.next(false).count()==30,"success resets retry");
    WeatherCache cache;
    const auto now = Clock::time_point{} + std::chrono::hours(100);
    auto wire = packet(cache, now);
    check(wire == "DASH/4\tkafel-weather\tKafel Weather\nTILE\tweather\t2\terror\tnormal\tPogoda\tmetric\t\t°C\tBrak danych\n", "empty cache golden packet");
    cache.update(*conditions, now);
    wire = packet(cache, now);
    check(wire == "DASH/4\tkafel-weather\tKafel Weather\nTILE\tweather\t2\tok\tnormal\tPogoda\tmetric-meter\t13\t°C\tSucho\tWiatr: 5 m/s\t1529\n", "unchanged weather appearance");
    auto decoded = kafel::parse_service_message(wire);
    check(decoded && decoded->tile_count == 1 && decoded->tiles[0].span == 2 &&
        decoded->tiles[0].metric->caption_count == 2 && decoded->tiles[0].metric->level == 1529, "real DASH/4 parser accepts packet");
    check(packet(cache, now+std::chrono::minutes(45)) == wire, "fresh at exact threshold and retained on failure");
    auto stale = kafel::parse_service_message(packet(cache, now+std::chrono::minutes(45)+std::chrono::seconds(1)));
    check(stale && stale->tiles[0].state == kafel::ServiceTileMessage::State::Warning &&
        stale->tiles[0].metric->value.view() == "13" && stale->tiles[0].metric->captions[0].view() == "Nieaktualne: Sucho", "stale retains value and warns");
    cache.update({-0.1, 5, Conditions::Kind::Rain}, now+std::chrono::hours(1));
    auto recovered = kafel::parse_service_message(packet(cache, now+std::chrono::hours(1)));
    check(recovered && recovered->tiles[0].metric->value.view()=="0" &&
        recovered->tiles[0].metric->captions[1].view()=="Wiatr: 1.4 m/s" &&
        recovered->tiles[0].state==kafel::ServiceTileMessage::State::Ok, "recovery, rounding and negative zero");
    cache.update({10, 0, Conditions::Kind::Clear}, now);
    check(kafel::parse_service_message(packet(cache,now))->tiles[0].metric->level==0,"calm has zero level");
    cache.update({10, 500, Conditions::Kind::Clear}, now);
    check(kafel::parse_service_message(packet(cache,now))->tiles[0].metric->level==10000,"wind saturates at configured maximum");
    // A private, disposable inbox exercises the actual Unix datagram transport.
    char directory[] = "/tmp/kafel-weather-test.XXXXXX";
    check(mkdtemp(directory)!=nullptr,"temporary directory");
    const std::string path = std::string(directory)+"/status.sock";
    const int fd = socket(AF_UNIX,SOCK_DGRAM,0);
    check(fd>=0,"test socket");
    sockaddr_un address{}; address.sun_family=AF_UNIX;
    std::memcpy(address.sun_path,path.c_str(),path.size()+1);
    check(bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"bind test inbox");
    timeval timeout{1,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    check(publish(path,wire)==0,"publish actual datagram");
    char buffer[8192]; const auto size=recv(fd,buffer,sizeof buffer,0);
    check(size>0 && std::string_view(buffer,static_cast<std::size_t>(size))==wire,"atomic exact snapshot");
    close(fd); unlink(path.c_str());
    check(publish(path,wire)==ENOENT,"missing dashboard recoverable");
    check(publish(std::string(200,'a'),wire)==EINVAL,"path bound");
    check(publish(path, std::string(8193, 'a'))==EMSGSIZE,"datagram size bound");
    check(publish(path, {})==EMSGSIZE,"empty datagram rejected");
    rmdir(directory);
    std::puts("PASS weather parsing, bounds, retry, cache, golden DASH/4, typography contract and Unix publishing");
}
