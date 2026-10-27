#include "appearance.hpp"
#include <array>
namespace kafel::weather {
namespace {
constexpr std::array catalog = {
    WmoAppearance{0, "Bezchmurnie", "clear-day", "clear-night"},
    WmoAppearance{1, "Pogodnie", "mostly-clear-day", "mostly-clear-night"},
    WmoAppearance{2, "Zachmurzenie", "partly-cloudy-day", "partly-cloudy-night"},
    WmoAppearance{3, "Pochmurno", "overcast-day", "overcast-night"},
    WmoAppearance{45, "Mgła", "fog-day", "fog-night"},
    WmoAppearance{48, "Mgła osadzająca szadź", "fog-day", "fog-night"},
    WmoAppearance{51, "Słaba mżawka", "overcast-day-drizzle", "overcast-night-drizzle"},
    WmoAppearance{53, "Mżawka", "overcast-day-drizzle", "overcast-night-drizzle"},
    WmoAppearance{55, "Silna mżawka", "extreme-day-drizzle", "extreme-night-drizzle"},
    WmoAppearance{56, "Słaba marznąca mżawka", "overcast-day-sleet", "overcast-night-sleet"},
    WmoAppearance{57, "Silna marznąca mżawka", "extreme-day-sleet", "extreme-night-sleet"},
    WmoAppearance{61, "Słaby deszcz", "overcast-day-rain", "overcast-night-rain"},
    WmoAppearance{63, "Deszcz", "overcast-day-rain", "overcast-night-rain"},
    WmoAppearance{65, "Silny deszcz", "extreme-day-rain", "extreme-night-rain"},
    WmoAppearance{66, "Słaby marznący deszcz", "overcast-day-sleet", "overcast-night-sleet"},
    WmoAppearance{67, "Silny marznący deszcz", "extreme-day-sleet", "extreme-night-sleet"},
    WmoAppearance{71, "Słaby śnieg", "overcast-day-snow", "overcast-night-snow"},
    WmoAppearance{73, "Śnieg", "overcast-day-snow", "overcast-night-snow"},
    WmoAppearance{75, "Silny śnieg", "extreme-day-snow", "extreme-night-snow"},
    WmoAppearance{77, "Śnieg ziarnisty", "snowflake", "snowflake"},
    WmoAppearance{80, "Słabe przelotne opady", "partly-cloudy-day-rain", "partly-cloudy-night-rain"},
    WmoAppearance{81, "Przelotny deszcz", "partly-cloudy-day-rain", "partly-cloudy-night-rain"},
    WmoAppearance{82, "Gwałtowne ulewy", "extreme-day-rain", "extreme-night-rain"},
    WmoAppearance{85, "Przelotny śnieg", "partly-cloudy-day-snow", "partly-cloudy-night-snow"},
    WmoAppearance{86, "Silny przelotny śnieg", "partly-cloudy-day-snow", "partly-cloudy-night-snow"},
    WmoAppearance{95, "Burza", "thunderstorms-day-rain", "thunderstorms-night-rain"},
    WmoAppearance{96, "Burza z drobnym gradem", "thunderstorms-day-hail", "thunderstorms-night-hail"},
    WmoAppearance{97, "Silna burza", "thunderstorms-extreme-day-rain", "thunderstorms-extreme-night-rain"},
    WmoAppearance{99, "Burza z silnym gradem", "thunderstorms-extreme-day-hail",
                  "thunderstorms-extreme-night-hail"},
};
}
std::span<const WmoAppearance> appearance_catalog() noexcept { return catalog; }
Appearance appearance(unsigned code, bool is_day) noexcept {
    for (const auto &entry : catalog)
        if (entry.code == code)
            return {is_day ? entry.day : entry.night, entry.description};
    return {"not-available", "Brak opisu"};
}
} // namespace kafel::weather
