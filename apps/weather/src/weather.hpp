#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kafel::weather {
using Clock = std::chrono::steady_clock;
struct Conditions {
    enum class Kind { Clear, Rain, Snow };
    double temperature;
    double wind_kmh;
    Kind kind;
    double apparent_temperature;
    double precipitation_mm;             // Water equivalent, including rain, showers and snow.
    std::int64_t precipitation_hour_end; // UTC epoch; sum over the preceding 3600 s.
    unsigned weather_code;               // Raw WMO code, independent of precipitation-based Kind.
    bool is_day;                         // Open-Meteo daylight flag, not guessed from local time.
};
// Only the current observation and its hourly precipitation are needed by the tile.
std::optional<Conditions> decode_forecast(std::string_view json);
std::optional<std::string> forecast_url(std::string_view latitude, std::string_view longitude);

class RetrySchedule {
  public:
    std::chrono::seconds next(bool success) noexcept;

  private:
    unsigned step_ = 0;
};
struct WeatherSnapshot {
    std::optional<Conditions> conditions;
    bool stale = false;
};
class WeatherCache {
  public:
    void update(Conditions conditions, Clock::time_point now) noexcept;
    WeatherSnapshot snapshot(Clock::time_point now) const noexcept;

  private:
    std::optional<Conditions> conditions_;
    Clock::time_point updated_{};
};
} // namespace kafel::weather
