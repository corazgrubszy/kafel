#include "weather.hpp"
#include "json/document.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace kafel::weather {
namespace {
std::optional<double> number(const picojson::value& value) {
    if (!value.is<double>() || !std::isfinite(value.get<double>())) return std::nullopt;
    return value.get<double>();
}
std::optional<double> coordinate(std::string_view input, double limit) {
    if (input.empty()) return std::nullopt;
    double value = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() ||
        !std::isfinite(value) || std::abs(value) > limit) return std::nullopt;
    return value;
}
std::string decimal(double value, unsigned precision) {
    char bytes[32];
    std::snprintf(bytes, sizeof bytes, "%.*f", static_cast<int>(precision), value == 0 ? 0 : value);
    std::string result(bytes);
    if (precision && result.ends_with(".0")) result.resize(result.size() - 2);
    return result;
}
}
std::optional<std::string> forecast_url(std::string_view latitude, std::string_view longitude) {
    const auto lat = coordinate(latitude, 90), lon = coordinate(longitude, 180);
    if (!lat || !lon) return std::nullopt;
    // Numeric reformatting prevents query injection. UTC epochs avoid timezone/DST ambiguity.
    return "https://api.open-meteo.com/v1/forecast?latitude=" + decimal(*lat, 6) +
        "&longitude=" + decimal(*lon, 6) +
        "&temperature_unit=celsius&windspeed_unit=kmh&current_weather=true&hourly=rain%2Csnowfall&forecast_days=7&timeformat=unixtime&timezone=GMT";
}
std::optional<Conditions> decode_forecast(std::string_view json) {
    const auto parsed=kafel::json::document(json);
    if(!parsed||!parsed->is<picojson::object>())return std::nullopt;
    const auto& root=*parsed;
    const auto& current = root.get("current_weather");
    const auto& hourly = root.get("hourly");
    if (!current.is<picojson::object>() || !hourly.is<picojson::object>()) return std::nullopt;
    const auto temperature = number(current.get("temperature"));
    const auto wind = number(current.get("windspeed"));
    const auto observed = number(current.get("time"));
    if (!temperature || !wind || !observed || *temperature < -100 || *temperature > 100 ||
        *wind < 0 || *wind > 500 || *observed < 0 || *observed > 253402300799.0 || std::floor(*observed) != *observed) return std::nullopt;
    const auto& times_value = hourly.get("time");
    const auto& rain_value = hourly.get("rain");
    const auto& snow_value = hourly.get("snowfall");
    if (!times_value.is<picojson::array>() || !rain_value.is<picojson::array>() ||
        !snow_value.is<picojson::array>()) return std::nullopt;
    const auto& times = times_value.get<picojson::array>();
    const auto& rain = rain_value.get<picojson::array>();
    const auto& snow = snow_value.get<picojson::array>();
    if (times.empty() || times.size() > 200 || rain.size() != times.size() || snow.size() != times.size())
        return std::nullopt;
    std::size_t index = 0;
    double previous = -1;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const auto time = number(times[i]), rainfall = number(rain[i]), snowfall = number(snow[i]);
        if (!time || !rainfall || !snowfall || *time <= previous || *time > 253402300799.0 || std::floor(*time) != *time ||
            (i && *time - previous != 3600) ||
            *rainfall < 0 || *snowfall < 0) return std::nullopt;
        previous = *time;
        if (*time <= *observed) index = i;
    }
    if (*observed < times.front().get<double>() || *observed >= previous + 3600) return std::nullopt;
    // Preserve InfoHub's precipitation-based classification, with snow taking precedence.
    const auto kind = snow[index].get<double>() > 0 ? Conditions::Kind::Snow :
                      rain[index].get<double>() > 0 ? Conditions::Kind::Rain : Conditions::Kind::Clear;
    return Conditions{*temperature, *wind, kind};
}
std::chrono::seconds RetrySchedule::next(bool success) noexcept {
    if (success) { step_ = 0; return std::chrono::minutes(15); }
    constexpr std::array delays{30, 60, 120, 300};
    const auto delay = delays[step_];
    if (step_ + 1 < delays.size()) ++step_;
    return std::chrono::seconds(delay);
}
void WeatherCache::update(Conditions conditions, Clock::time_point now) noexcept {
    conditions_ = conditions;
    updated_ = now;
}
WeatherSnapshot WeatherCache::snapshot(Clock::time_point now) const noexcept {
    if (!conditions_ || now < updated_) return {};
    return {conditions_, now - updated_ > std::chrono::minutes(45)};
}
}
