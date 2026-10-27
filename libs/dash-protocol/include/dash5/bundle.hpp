#pragma once
#include "protocol.hpp"
namespace kafel::dash5 {
struct AssetBundle {
    Resources resources;
    std::string template_id;
    std::map<std::string, std::string, std::less<>> svg;
};
AssetBundle asset_bundle(std::string_view embedded_json);
} // namespace kafel::dash5
