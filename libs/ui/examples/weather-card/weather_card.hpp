#pragma once
#include "ui/composition_json.hpp"
#include "ui/svg.hpp"
#include <vector>
namespace weather_example {
inline constexpr int extent = 240;
struct Data {
    int temperature = 13;
    int apparent_temperature = 11;
    double wind_ms = 5;
    double precipitation_mm = 0;
    std::string_view description{};
    unsigned weather_code = 2;
    bool is_day = true;
};
// Presentation study only: no protocol, device access or dependency on the daemon.
class WeatherCard {
  public:
    WeatherCard(const char *regular_font, const char *bold_font, const char *template_path,
                const char *svg_path);
    void set_svg(std::string_view source);
    void paint(ui::Surface &surface, const Data &data = {}) const;

  private:
    ui::CompositionDocument document_;
    std::vector<std::unique_ptr<ui::TextFont>> fonts_;
    std::vector<const ui::TextFont *> faces_;
    ui::CompositionPainter painter_;
    std::vector<unsigned char> rgba_;
};
} // namespace weather_example
