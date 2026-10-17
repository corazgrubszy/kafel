#include "packet.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
namespace kafel::weather {
namespace {
std::string decimal(double value, unsigned precision) {
    char bytes[32];
    std::snprintf(bytes, sizeof bytes, "%.*f", static_cast<int>(precision), value == 0 ? 0 : value);
    std::string result(bytes);
    if (precision && result.ends_with(".0")) result.resize(result.size() - 2);
    return result;
}
}
std::string dashboard_packet(const WeatherSnapshot& snapshot) {
    const std::string prefix = "DASH/4\tkafel-weather\tKafel Weather\nTILE\tweather\t2\t";
    if (!snapshot.conditions)
        return prefix + "error\tnormal\tPogoda\tmetric\t\t°C\tBrak danych\n";
    const auto& data = *snapshot.conditions;
    const char* label = data.kind == Conditions::Kind::Snow ? "Śnieg" :
                        data.kind == Conditions::Kind::Rain ? "Deszcz" : "Sucho";
    return prefix + (snapshot.stale ? "warning" : "ok") + "\tnormal\tPogoda\tmetric-meter\t" +
        decimal(std::round(data.temperature), 0) + "\t°C\t" +
        (snapshot.stale ? "Nieaktualne: " : "") + label + "\tWiatr: " +
        decimal(std::round(data.wind_kmh / 3.6 * 10) / 10, 1) + " m/s\t" +
        std::to_string(static_cast<unsigned>(std::lround(std::clamp(data.wind_kmh / 3.6 / 32.7,0.0,1.0)*10000))) + "\n";
}
}
