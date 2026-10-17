#include "source.hpp"
namespace kafel::weather {
FetchResult fetch(const net::HttpClient& client, std::string_view url) {
    const auto response = client.get(url, {std::chrono::seconds(5), 65536});
    if (!response) return FetchError{response.error().message};
    if (response->status != 200) return FetchError{"HTTP " + std::to_string(response->status)};
    const auto conditions = decode_forecast(response->body);
    if (!conditions) return FetchError{"Invalid Open-Meteo forecast"};
    return *conditions;
}
}
