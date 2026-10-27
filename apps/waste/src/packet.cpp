#include "packet.hpp"
#include <array>
namespace kafel::waste {
extern "C" const unsigned char *kafel_waste_assets_data();
extern "C" std::size_t kafel_waste_assets_size();
const dash5::AssetBundle &dashboard_assets() {
    static const auto bundle = dash5::asset_bundle(
        {reinterpret_cast<const char *>(kafel_waste_assets_data()), kafel_waste_assets_size()});
    return bundle;
}
namespace {
constexpr std::array months = {"sty", "lut", "mar", "kwi", "maj", "cze",
                               "lip", "sie", "wrz", "paź", "lis", "gru"};
std::string_view month_name(Date day) {
    return months[unsigned(std::chrono::year_month_day(day).month()) - 1];
}
std::string day_number(Date day) { return std::to_string(unsigned(std::chrono::year_month_day(day).day())); }
std::string fraction_names(unsigned mask) {
    constexpr std::array labels = {"Zmieszane", "Szkło", "Papier", "Tworzywa", "Kuchenne", "Ogród"};
    std::string result;
    for (unsigned i = 0; i < labels.size(); ++i)
        if (mask & (1u << i)) {
            if (!result.empty())
                result += " · ";
            result += labels[i];
        }
    return result;
}
std::string next_pickup(const std::optional<Day> &next, Date today) {
    if (!next)
        return "Brak kolejnych terminów";
    const auto date = next->date == today + std::chrono::days(1)
                          ? std::string("Jutro")
                          : day_number(next->date) + " " + std::string(month_name(next->date));
    return date + ": " + fraction_names(next->fractions);
}
} // namespace
dash5::Json dashboard_document(const Schedule *schedule, Date today, bool failed) {
    using namespace dash5;
    Object tile{{"id", Json("waste")}, {"title", Json("Śmieci")}, {"attention", Json(false)}};
    if (!schedule) {
        tile["state"] = Json("error");
        tile["freshness"] = Json("missing");
        tile["message"] = Json("Brak harmonogramu");
        tile["observed_at"] = Json();
        tile["content"] = Json();
    } else {
        const auto selected = select(*schedule, today);
        std::string caption = "Brak", names;
        unsigned fractions = 0;
        if (selected) {
            if (selected->when == Selection::When::Today) {
                caption = "Dzisiaj";
                fractions = selected->day.fractions;
                names = next_pickup(selected->next, today);
            } else {
                names = fraction_names(selected->day.fractions);
                if (selected->when == Selection::When::Tomorrow) {
                    caption = "Jutro";
                    fractions = selected->day.fractions;
                } else {
                    caption =
                        day_number(selected->day.date) + " " + std::string(month_name(selected->day.date));
                    // A distant pickup is a date reminder, without bins to put out yet.
                }
            }
            tile["attention"] = Json(selected->attention);
        }
        const auto &assets = dashboard_assets();
        tile["state"] = Json(failed || !selected ? "warning" : "ok");
        tile["freshness"] = Json(failed ? "stale" : "fresh");
        tile["message"] = Json(failed ? "Nieaktualny harmonogram" : !selected ? "Brak terminów" : "");
        tile["observed_at"] =
            Json(double(std::chrono::duration_cast<std::chrono::seconds>(today.time_since_epoch()).count()));
        tile["content"] = Json(
            Object{{"template", Json(assets.template_id)},
                   {"data", Json(Object{{"caption", Json(caption)},
                                        {"fractions", Json(selected ? names : "Brak terminów")},
                                        {"illustration", Json(assets.svg.at(std::to_string(fractions)))}})}});
    }
    return Json(Array{Json(tile)});
}
std::string dashboard_tiles(const Schedule *schedule, Date today, bool failed) {
    return dashboard_document(schedule, today, failed).serialize();
}
} // namespace kafel::waste
