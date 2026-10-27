#include "weather_card.hpp"
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
int main(int argc, char **argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--icon") {
        try {
            ui::SvgDocument svg(ui::read_svg_file(argv[2]));
            ui::SvgRasterizer rasterizer;
            std::array<unsigned char, 128 * 128 * 4> rgba{};
            rasterizer.render(svg, 128, 128, rgba);
            std::array<unsigned char, 128 * 128 * 3> rgb{};
            constexpr std::array<unsigned, 3> base = {35, 33, 54};
            for (std::size_t i = 0; i < 128 * 128; ++i)
                for (std::size_t c = 0; c < 3; ++c)
                    rgb[i * 3 + c] = static_cast<unsigned char>(
                        (rgba[i * 4 + c] * rgba[i * 4 + 3] + base[c] * (255 - rgba[i * 4 + 3]) + 127) / 255);
            std::ofstream file(argv[3], std::ios::binary);
            file << "P6\n128 128\n255\n";
            file.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
            return file ? 0 : 1;
        } catch (const std::exception &error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
    if (argc != 6 && argc != 7 && argc != 8)
        return 2;
    try {
        std::array<unsigned char, weather_example::extent * weather_example::extent * 3> pixels{};
        ui::Surface surface(240, 240, pixels);
        weather_example::WeatherCard card(argv[1], argv[2], argv[3], argv[4]);
        weather_example::Data data;
        if (argc == 8) {
            const std::string_view code = argv[6], day = argv[7];
            const auto parsed = std::from_chars(code.data(), code.data() + code.size(), data.weather_code);
            if (parsed.ec != std::errc{} || parsed.ptr != code.data() + code.size() ||
                (day != "0" && day != "1"))
                throw std::invalid_argument("invalid WMO/daylight arguments");
            data.is_day = day == "1";
        } else if (argc == 7) {
            const std::string_view scene = argv[6];
            if (scene == "sun")
                data = {22, 24, 2.5, 0, "Słonecznie", 0};
            else if (scene == "clouds")
                data = {13, 11, 5, 0, "Pochmurno", 3};
            else if (scene == "rain")
                data = {9, 6, 7.2, 2.4, "Deszcz", 63};
            else if (scene == "snow")
                data = {-2, -6, 4.1, 0.8, "Śnieg", 73};
            else
                throw std::invalid_argument("unknown weather scene");
        }
        card.paint(surface, data);
        std::ofstream file(argv[5], std::ios::binary);
        file << "P6\n240 240\n255\n";
        file.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
        if (!file)
            return 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
