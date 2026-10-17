#pragma once
#include "weather.hpp"
namespace kafel::weather {
// Presentation policy belongs to the producer; renderer and protocol stay generic.
std::string dashboard_packet(const WeatherSnapshot& snapshot);
}
