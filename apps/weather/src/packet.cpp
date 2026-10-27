#include "packet.hpp"
#include "appearance.hpp"
namespace kafel::weather {
extern "C" const unsigned char *kafel_weather_assets_data();
extern "C" std::size_t kafel_weather_assets_size();
const dash5::AssetBundle &dashboard_assets() {
    static const auto bundle = dash5::asset_bundle(
        {reinterpret_cast<const char *>(kafel_weather_assets_data()), kafel_weather_assets_size()});
    return bundle;
}
dash5::Json dashboard_document(const WeatherSnapshot &snapshot) {
    using namespace dash5;
    Object tile{{"id", Json("weather")}, {"title", Json("Pogoda")}, {"attention", Json(false)}};
    if (!snapshot.conditions) {
        tile["state"] = Json("error");
        tile["freshness"] = Json("missing");
        tile["observed_at"] = Json();
        tile["message"] = Json("Brak danych");
        tile["content"] = Json();
    } else {
        const auto &data = *snapshot.conditions;
        auto look = appearance(data.weather_code, data.is_day);
        const auto &assets = dashboard_assets();
        Object fields{{"temperature", Json(data.temperature)},
                      {"apparent_temperature", Json(data.apparent_temperature)},
                      {"wind_ms", Json(data.wind_kmh / 3.6)},
                      {"precipitation_mm", Json(data.precipitation_mm)},
                      {"description", Json(std::string(look.description))},
                      {"illustration", Json(assets.svg.at(std::string(look.illustration)))}};
        tile["state"] = Json(snapshot.stale ? "warning" : "ok");
        tile["freshness"] = Json(snapshot.stale ? "stale" : "fresh");
        tile["observed_at"] = Json(double(data.precipitation_hour_end));
        tile["message"] = Json(snapshot.stale ? "Nieaktualne dane" : "");
        tile["content"] = Json(Object{{"template", Json(assets.template_id)}, {"data", Json(fields)}});
    }
    return Json(Array{Json(tile)});
}
std::string dashboard_tiles(const WeatherSnapshot &snapshot) {
    return dashboard_document(snapshot).serialize();
}
} // namespace kafel::weather
