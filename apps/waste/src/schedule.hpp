#pragma once
#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace kafel::waste {
using Date = std::chrono::sys_days;
struct Day {
    Date date;
    unsigned fractions = 0;
};
struct Schedule {
    std::string timezone;
    std::vector<Day> days;
};
struct Error {
    std::string message;
};
using ScheduleResult = std::variant<Schedule, Error>;
inline constexpr std::size_t maximum_schedule_bytes = 256 * 1024;
ScheduleResult parse_schedule(std::string_view json);
ScheduleResult load_schedule(const char *path);
std::optional<Date> parse_date(std::string_view text) noexcept;
// Timezone belongs to this single-threaded daemon. No dashboard timezone changes.
bool activate_timezone(std::string_view zone);
std::optional<Date> local_date(std::time_t now) noexcept;
class ScheduleCache {
  public:
    std::optional<Error> reload(const char *path);
    const Schedule *get() const noexcept { return schedule_ ? &*schedule_ : nullptr; }
    bool degraded() const noexcept { return failed_; }

  private:
    std::optional<Schedule> schedule_;
    bool failed_ = false;
};
struct Selection {
    enum class When { Today, Tomorrow, Next };
    Day day;
    std::optional<Day> next;
    When when;
    bool attention;
};
std::optional<Selection> select(const Schedule &, Date today) noexcept;
} // namespace kafel::waste
