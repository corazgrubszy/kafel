#pragma once
#include <span>
#include <string_view>
namespace kafel::weather {
struct Appearance {
    std::string_view illustration, description;
};
struct WmoAppearance {
    unsigned code;
    std::string_view description, day, night;
};
// Weather owns WMO semantics. UI receives only an opaque SVG resource.
// The static catalog also drives documentation and the offline icon overview.
std::span<const WmoAppearance> appearance_catalog() noexcept;
Appearance appearance(unsigned wmo_code, bool is_day) noexcept;
} // namespace kafel::weather
