#pragma once
#include "dash5/bundle.hpp"
#include "weather.hpp"
namespace kafel::weather {
const dash5::AssetBundle &dashboard_assets();
dash5::Json dashboard_document(const WeatherSnapshot &);
std::string dashboard_tiles(const WeatherSnapshot &);
} // namespace kafel::weather
