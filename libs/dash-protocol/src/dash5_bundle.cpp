#include "dash5/bundle.hpp"
namespace kafel::dash5 {
AssetBundle asset_bundle(std::string_view bytes) {
    picojson::value root;
    std::string error;
    picojson::parse(root, bytes.begin(), bytes.end(), &error);
    if (!error.empty())
        throw std::invalid_argument("invalid embedded assets");
    AssetBundle bundle;
    auto add = [&](const std::string &data, const char *mime) {
        auto id = resource_id(data);
        bundle.resources[id] = std::make_shared<Resource>(Resource{id, mime, data});
        return id;
    };
    bundle.template_id = add(root.get("template").get<std::string>(), "application/vnd.kafel.tile+json");
    for (const auto &[name, svg] : root.get("svg").get<Object>())
        bundle.svg[name] = add(svg.get<std::string>(), "image/svg+xml");
    return bundle;
}
} // namespace kafel::dash5
