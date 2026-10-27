#include "weather_card.hpp"
#include "appearance.hpp"
#include <stdexcept>
namespace weather_example {
namespace {
std::vector<std::unique_ptr<ui::TextFont>> fonts(const ui::CompositionDocument &document, const char *regular,
                                                 const char *bold) {
    std::vector<std::unique_ptr<ui::TextFont>> result;
    for (const auto &face : document.fonts()) {
        const char *path = face.id == "regular" ? regular : face.id == "bold" ? bold : nullptr;
        if (!path)
            throw std::invalid_argument("unsupported example font resource");
        result.push_back(std::make_unique<ui::TextFont>(path, face.repertoire, face.styles));
    }
    return result;
}
std::vector<const ui::TextFont *> faces(const std::vector<std::unique_ptr<ui::TextFont>> &fonts) {
    std::vector<const ui::TextFont *> result;
    for (const auto &font : fonts)
        result.push_back(font.get());
    return result;
}
} // namespace
WeatherCard::WeatherCard(const char *regular, const char *bold, const char *path, const char *svg_path)
    : document_(ui::CompositionDocument::from_file(path)), fonts_(fonts(document_, regular, bold)),
      faces_(faces(fonts_)), painter_(document_.composition(), faces_) {
    set_svg(ui::read_svg_file(svg_path));
}
void WeatherCard::set_svg(std::string_view source) {
    ui::SvgDocument svg(source);
    ui::SvgRasterizer rasterizer;
    std::vector<unsigned char> replacement(extent * extent * 4);
    rasterizer.render(svg, extent, extent, replacement);
    rgba_.swap(replacement);
}

void WeatherCard::paint(ui::Surface &surface, const Data &data) const {
    const auto look = kafel::weather::appearance(data.weather_code, data.is_day);
    const std::array values = {
        ui::Field{"temperature", static_cast<double>(data.temperature)},
        ui::Field{"apparent_temperature", static_cast<double>(data.apparent_temperature)},
        ui::Field{"wind_ms", data.wind_ms},
        ui::Field{"precipitation_mm", data.precipitation_mm},
        ui::Field{"description", data.description.empty() ? look.description : data.description},
    };
    const ui::RgbaImage image{extent, extent, rgba_};
    painter_.paint(surface, values, &image);
}
} // namespace weather_example
