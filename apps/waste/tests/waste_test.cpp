#include "dash5/protocol.hpp"
#include "packet.hpp"
#include "schedule.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
using namespace kafel::waste;
void check(bool condition, const char *label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
const std::string sample =
    R"({"timezone":"Europe/Warsaw","pickups":[{"date":"2026-10-19","fraction":"paper"},{"date":"2026-10-18","fraction":"glass"},{"date":"2026-10-17","fraction":"plastic"},{"date":"2026-10-17","fraction":"mixed"},{"date":"2026-10-17","fraction":"plastic"}]})";
std::time_t utc(int year, int month, int day, int hour, int minute) {
    const auto date = std::chrono::sys_days(std::chrono::year(year) / month / day);
    return std::chrono::duration_cast<std::chrono::seconds>(date.time_since_epoch()).count() + hour * 3600 +
           minute * 60;
}
int main() {
    auto result = parse_schedule(sample);
    check(std::holds_alternative<Schedule>(result), "valid JSON");
    const auto schedule = std::get<Schedule>(result);
    check(schedule.days.size() == 3 && schedule.days[0].fractions == 9,
          "dates sorted, fractions deduplicated and grouped");
    const auto today = *parse_date("2026-10-17");
    const auto a = select(schedule, today);
    check(a && a->when == Selection::When::Today && a->next &&
              a->next->date == today + std::chrono::days(1) && a->attention,
          "today selection keeps following pickup independently");
    using namespace kafel::dash5;
    auto tile = [](std::string wire) { return parse(wire).get<Array>().at(0); };
    auto current = tile(dashboard_tiles(&schedule, today));
    check(current.get("attention") == Json(true) && current.get("state") == Json("ok"),
          "pickup attention independent from ok state");
    check(current.get("content").get("data").get("caption") == Json("Dziś"), "approved titlecase caption");
    check(current.get("content").get("data").get("fractions") == Json("Jutro: Szkło"),
          "today card announces tomorrow without rotating bins or title");
    const Schedule only_tomorrow{"UTC", {{today + std::chrono::days(1), 4}}};
    auto tomorrow = tile(dashboard_tiles(&only_tomorrow, today));
    check(tomorrow.get("content").get("data").get("caption") == Json("Jutro") &&
              tomorrow.get("content").get("data").get("fractions") == Json("Papier"),
          "tomorrow title and matching fractions when no pickup today");
    auto future = tile(dashboard_tiles(&schedule, *parse_date("2026-10-15")));
    check(future.get("content").get("data").get("caption") == Json("17 paź") &&
              !future.get("content").get("data").contains("month"),
          "nearest date is a single Polish caption without a separate month field");
    check(future.get("content").get("data").get("illustration") == Json(dashboard_assets().svg.at("0")) &&
              future.get("content").get("data").get("fractions") == Json("Zmieszane · Tworzywa") &&
              future.get("attention") == Json(false),
          "distant pickup has date and fractions but no bins or attention");
    check(current.get("content").get("data").get("illustration") == Json(dashboard_assets().svg.at("9")),
          "today bins stay today's even with tomorrow pickup");
    check(tomorrow.get("content").get("data").get("illustration") == Json(dashboard_assets().svg.at("4")),
          "tomorrow bins match tomorrow fractions");
    const Schedule later{"UTC", {{today, 1}, {today + std::chrono::days(3), 12}}};
    auto upcoming = tile(dashboard_tiles(&later, today));
    check(upcoming.get("content").get("data").get("fractions") == Json("20 paź: Papier · Tworzywa"),
          "today footer announces nearest later day and all its fractions");
    const Schedule last{"UTC", {{today, 1}}};
    check(tile(dashboard_tiles(&last, today)).get("content").get("data").get("fractions") ==
              Json("Brak kolejnych terminów"),
          "last pickup has an explicit no-following-date caption");
    check(!select(last, today)->next, "selection has no dangling following day at end");
    const Schedule empty{"UTC", {}};
    check(!select(empty, today) && tile(dashboard_tiles(&empty, today)).get("state") == Json("warning"),
          "empty valid schedule has no synthetic pickup");
    const auto year_end = *parse_date("2026-12-31");
    const Schedule year_boundary{"UTC", {{year_end, 1}, {year_end + std::chrono::days(1), 2}}};
    check(tile(dashboard_tiles(&year_boundary, year_end)).get("content").get("data").get("fractions") ==
              Json("Jutro: Szkło"),
          "following-day label crosses year boundary correctly");
    const auto leap_day = *parse_date("2028-02-28");
    const Schedule leap_boundary{"UTC", {{leap_day, 1}, {leap_day + std::chrono::days(2), 4}}};
    check(tile(dashboard_tiles(&leap_boundary, leap_day)).get("content").get("data").get("fractions") ==
              Json("1 mar: Papier"),
          "next date respects leap day rather than assuming consecutive pickups");
    const Schedule longest{"UTC", {{today, 1}, {today + std::chrono::days(3), 63}}};
    const auto long_data = tile(dashboard_tiles(&longest, today)).get("content").get("data");
    check(long_data.get("fractions").get<std::string>().size() <= 96 &&
              long_data.get("fractions") ==
                  Json("20 paź: Zmieszane · Szkło · Papier · Tworzywa · Kuchenne · Ogród"),
          "all six next fractions fit existing data limit without truncation");
    auto missing = tile(dashboard_tiles(nullptr, today));
    check(missing.get("state") == Json("error") && missing.get("content").is<picojson::null>(),
          "missing schedule error");
    auto expired = tile(dashboard_tiles(&schedule, *parse_date("2027-01-01")));
    check(expired.get("state") == Json("warning") && expired.get("message") == Json("Brak terminów"),
          "no future pickup is explicit");
    const auto &assets = dashboard_assets();
    auto bin = assets.resources.at(assets.svg.at("48"))->bytes;
    check(bin.find("#A0522D") != bin.npos && bin.find("#008F39") != bin.npos,
          "kitchen brown, garden glass-green");
    for (const auto &[id, resource] : assets.resources)
        check(id == resource_id(resource->bytes), "embedded bin resources immutable and hashed");
    for (auto bad : {"2026-02-29", "2026-13-01", "2026-10-32", "2026-1-01", "0000-01-01", "2026-01-01x"})
        check(!parse_date(bad), "invalid civil dates rejected");
    check(parse_date("2028-02-29").has_value(), "leap day accepted");
    for (const auto &bad :
         {std::string("{}"), sample + "{}", std::string("{\"timezone\":\"../UTC\",\"pickups\":[]}"),
          std::string(
              "{\"timezone\":\"UTC\",\"pickups\":[{\"date\":\"2026-02-29\",\"fraction\":\"paper\"}]}"),
          std::string(
              "{\"timezone\":\"UTC\",\"pickups\":[{\"date\":\"2026-10-17\",\"fraction\":\"unknown\"}]}"),
          std::string("{\"timezone\":\"UTC\",\"pickups\":[null]}"),
          std::string(33, '[') + "0" + std::string(33, ']'), std::string(maximum_schedule_bytes + 1, ' ')})
        check(std::holds_alternative<Error>(parse_schedule(bad)), "malformed schedule rejected atomically");
    check(activate_timezone("Europe/Warsaw"), "timezone database available");
    check(local_date(utc(2026, 3, 29, 22, 30)) == parse_date("2026-03-30"), "spring DST local date boundary");
    check(local_date(utc(2026, 10, 25, 23, 30)) == parse_date("2026-10-26"),
          "autumn DST local date boundary");
    check(local_date(utc(2026, 12, 31, 23, 30)) == parse_date("2027-01-01"), "local new year");
    check(!activate_timezone("../etc/passwd") && !activate_timezone("Missing/Zone"),
          "timezone traversal and missing zone rejected");
    const auto boundary = parse_schedule(
        R"({"timezone":"Europe/Warsaw","pickups":[{"date":"2027-01-01","fraction":"paper"}]})");
    check(select(std::get<Schedule>(boundary), *parse_date("2026-12-31"))->when == Selection::When::Tomorrow,
          "tomorrow across year boundary");
    const auto leap =
        parse_schedule(R"({"timezone":"UTC","pickups":[{"date":"2028-02-29","fraction":"paper"}]})");
    check(select(std::get<Schedule>(leap), *parse_date("2028-02-28"))->when == Selection::When::Tomorrow,
          "tomorrow across leap boundary");
    char path[] = "/tmp/kafel-waste-test.XXXXXX";
    const int fd = mkstemp(path);
    check(fd >= 0, "temporary fixture");
    close(fd);
    {
        std::ofstream file(path);
        file << sample;
    }
    check(std::holds_alternative<Schedule>(load_schedule(path)), "real bounded file load");
    ScheduleCache cache;
    check(!cache.reload(path) && cache.get() && !cache.degraded(), "cache accepts complete schedule");
    const auto count = cache.get()->days.size();
    {
        std::ofstream file(path);
        file << "{";
    }
    check(cache.reload(path).has_value() && cache.get()->days.size() == count && cache.degraded(),
          "failed reload retains previous complete schedule");
    auto degraded = tile(dashboard_tiles(cache.get(), today, cache.degraded()));
    check(degraded.get("state") == Json("warning") && degraded.get("attention") == Json(true),
          "reload failure retains attention");
    {
        std::ofstream file(path);
        file << sample;
    }
    check(!cache.reload(path) && !cache.degraded(), "cache recovers after repaired file");
    {
        std::ofstream file(path);
        file << "{";
    }
    check(std::holds_alternative<Error>(load_schedule(path)), "truncated file rejected");
    unlink(path);
    check(std::holds_alternative<Error>(load_schedule(path)), "missing file rejected");
    check(std::holds_alternative<Error>(load_schedule("/tmp")), "non-regular input rejected");
    std::puts(
        "PASS waste schedule, validation, grouping, calendar/DST, attention, large DASH/5 and file errors");
}
