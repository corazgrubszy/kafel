#include "dash5/unix.hpp"
#include "packet.hpp"
#include "weather.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace kafel::weather;
using namespace kafel::dash5;
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}
std::string packet(const WeatherCache &cache, Clock::time_point now) {
    return dashboard_tiles(cache.snapshot(now));
}
const std::string valid =
    R"({"current":{"temperature_2m":12.8,"apparent_temperature":11.2,"wind_speed_10m":18,"weather_code":2,"is_day":1,"time":1800000900},"hourly":{"time":[1800000000,1800003600],"rain":[0,1],"snowfall":[0,0],"precipitation":[0.4,1.5]}})";
std::string replace(std::string input, std::string_view from, std::string_view to) {
    const auto position = input.find(from);
    check(position != input.npos, "test replacement exists");
    input.replace(position, from.size(), to);
    return input;
}
int main() {
    const auto conditions = decode_forecast(valid);
    check(conditions && conditions->temperature == 12.8 && conditions->wind_kmh == 18 &&
              conditions->kind == Conditions::Kind::Clear,
          "current hour, not future rain");
    check(decode_forecast(replace(valid, "[0,1]", "[1,1]"))->kind == Conditions::Kind::Rain, "rain");
    check(decode_forecast(replace(replace(valid, "[0,1]", "[1,1]"), "[0,0]", "[1,0]"))->kind ==
              Conditions::Kind::Snow,
          "snow precedence");
    for (const auto &bad :
         {std::string("{}"), valid + "{}", replace(valid, "12.8", "null"), replace(valid, "12.8", "\"12.8\""),
          replace(valid, "12.8", "101"), replace(valid, "12.8", "1e999"),
          replace(valid, "\"wind_speed_10m\":18", "\"wind_speed_10m\":-1"), replace(valid, "[0,1]", "[0]"),
          replace(valid, "[0,1]", "[0,null]"), replace(valid, "[0,0]", "[0,-1]"),
          replace(valid, "1800003600", "1800000000"), replace(valid, "1800003600", "1800007200"),
          replace(valid, "1800000900", "1800000900.5"), replace(valid, "1800000900", "1700000000"),
          replace(valid, "1800000900", "1900000000"), std::string(33, '[') + "0" + std::string(33, ']'),
          std::string(65537, ' '), valid.substr(0, valid.size() - 1)})
        check(!decode_forecast(bad), "reject malformed/incomplete/out-of-range forecast");
    check(conditions->apparent_temperature == 11.2 && conditions->precipitation_mm == 0.4 &&
              conditions->precipitation_hour_end == 1800000000 && conditions->weather_code == 2 &&
              conditions->is_day,
          "retain apparent temperature, hourly water equivalent, period end and raw WMO");
    const auto next_hour = decode_forecast(replace(valid, "1800000900", "1800003600"));
    check(next_hour && next_hour->precipitation_mm == 1.5 && next_hour->precipitation_hour_end == 1800003600,
          "hour boundary selects the new completed precipitation period");
    const auto last = decode_forecast(replace(valid, "1800000900", "1800007199"));
    check(last && last->precipitation_hour_end == 1800003600, "last second of final hour");
    check(!decode_forecast(replace(valid, "1800000900", "1800007200")), "past final supported hour");
    for (auto code : {0,  1,  2,  3,  45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66,
                      67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 97, 99}) {
        const auto result =
            decode_forecast(replace(valid, "\"weather_code\":2", "\"weather_code\":" + std::to_string(code)));
        check(result && result->weather_code == static_cast<unsigned>(code),
              "preserve WMO code including severe thunderstorm");
    }
    for (const auto &bad :
         {replace(valid, "\"apparent_temperature\":11.2", "\"missing\":11.2"), replace(valid, "11.2", "null"),
          replace(valid, "11.2", "\"11.2\""), replace(valid, "11.2", "-151"), replace(valid, "11.2", "101"),
          replace(valid, "11.2", "1e999"), replace(valid, "\"weather_code\":2", "\"missing\":2"),
          replace(valid, "\"weather_code\":2", "\"weather_code\":null"),
          replace(valid, "\"weather_code\":2", "\"weather_code\":\"2\""),
          replace(valid, "\"weather_code\":2", "\"weather_code\":-1"),
          replace(valid, "\"weather_code\":2", "\"weather_code\":100"),
          replace(valid, "\"weather_code\":2", "\"weather_code\":2.5"),
          replace(valid, "\"precipitation\":[0.4,1.5]", "\"missing\":[0.4,1.5]"),
          replace(valid, "[0.4,1.5]", "null"), replace(valid, "[0.4,1.5]", "[0.4]"),
          replace(valid, "[0.4,1.5]", "[0.4,null]"), replace(valid, "[0.4,1.5]", "[0.4,-1]"),
          replace(valid, "[0.4,1.5]", "[0.4,1001]"), replace(valid, "[0.4,1.5]", "[0.4,1e999]")})
        check(!decode_forecast(bad), "new fields are required, typed and bounded across the whole response");
    const auto night = decode_forecast(replace(valid, "\"is_day\":1", "\"is_day\":0"));
    check(night && !night->is_day, "night is retained, not inferred from timestamp");
    WeatherCache night_cache;
    night_cache.update(*night, Clock::time_point{});
    check(!night_cache.snapshot(Clock::time_point{}).conditions->is_day, "cache retains night flag");
    for (auto bad : {"null", "true", "false", "\"1\"", "-1", "2", "0.5"})
        check(!decode_forecast(replace(valid, "\"is_day\":1", std::string("\"is_day\":") + bad)),
              "reject invalid daylight flag");
    check(!decode_forecast(replace(valid, "\"is_day\":1,", "")), "daylight flag is required");
    check(decode_forecast(valid + " \n\t").has_value(), "trailing whitespace");
    check(forecast_url("52.123456", "17").has_value(), "coordinates");
    check(forecast_url("-90", "180").has_value(), "coordinate boundaries");
    for (auto bad : {"", "nan", "inf", "91", "52&evil=1", "52 ", "1e999"})
        check(!forecast_url(bad, "17"), "reject bad latitude");
    check(!forecast_url("52", "181"), "longitude range");
    check(forecast_url("52", "17")->find("timeformat=unixtime&timezone=GMT") != std::string::npos,
          "UTC epoch request");
    const auto request = *forecast_url("52", "17");
    check(request.find(
              "current=temperature_2m%2Capparent_temperature%2Cwind_speed_10m%2Cweather_code%2Cis_day") !=
                  request.npos &&
              request.find("hourly=rain%2Csnowfall%2Cprecipitation") != request.npos &&
              request.find("precipitation_unit=mm") != request.npos,
          "request exact current fields and hourly precipitation in mm");
    check(!valid_socket_path({}) && !valid_socket_path(std::string_view("a\0b", 3)), "invalid socket paths");
    RetrySchedule retry;
    for (int expected : {30, 60, 120, 300, 300, 300})
        check(retry.next(false).count() == expected, "retry saturation");
    check(retry.next(true).count() == 900 && retry.next(false).count() == 30, "success resets retry");
    WeatherCache cache;
    const auto now = Clock::time_point{} + std::chrono::hours(100);
    auto tile = [](const std::string &wire) {
        return kafel::dash5::parse(wire).get<kafel::dash5::Array>().at(0);
    };
    auto missing = tile(packet(cache, now));
    check(missing.get("state") == Json("error") && missing.get("content").is<picojson::null>(),
          "missing weather has no stale numeric value");
    cache.update(*conditions, now);
    const auto wire = packet(cache, now);
    const auto good = tile(wire);
    check(good.get("state") == Json("ok") && good.get("freshness") == Json("fresh"), "fresh state");
    const auto &data = good.get("content").get("data");
    check(data.get("temperature") == Json(12.8) && data.get("apparent_temperature") == Json(11.2) &&
              data.get("wind_ms") == Json(5.),
          "unrounded measurements, renderer owns formatting");
    const auto &resources = dashboard_assets();
    check(data.get("illustration") == Json(resources.svg.at("partly-cloudy-day")),
          "WMO selection stays in weather producer");
    check(packet(cache, now + std::chrono::minutes(45)) == wire, "fresh at exact stale boundary");
    auto stale = tile(packet(cache, now + std::chrono::minutes(45) + std::chrono::seconds(1)));
    check(stale.get("state") == Json("warning") && stale.get("freshness") == Json("stale") &&
              stale.get("content") == good.get("content"),
          "stale state retains a complete matching illustration and measurements");
    auto night_conditions = *conditions;
    night_conditions.is_day = false;
    cache.update(night_conditions, now);
    auto night_tile = tile(packet(cache, now));
    check(night_tile.get("content").get("data").get("illustration") ==
              Json(resources.svg.at("partly-cloudy-night")),
          "night changes SVG reference");
    for (const auto &[id, resource] : resources.resources)
        check(id == resource_id(resource->bytes), "embedded resources have exact SHA-256");
    check(cache.snapshot(now + std::chrono::minutes(46)).conditions->weather_code == 2,
          "stale cache keeps raw source fields");
    std::puts(
        "PASS weather decoding, strict is_day, retry, cache and native DASH/5 tiles/day-night resources");
}
