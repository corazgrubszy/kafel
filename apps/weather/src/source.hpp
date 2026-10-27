#pragma once
#include "net/http_client.hpp"
#include "weather.hpp"
#include <variant>
namespace kafel::weather {
struct FetchError {
    std::string message;
};
using FetchResult = std::variant<Conditions, FetchError>;
// Transport, HTTP status and payload validation form one atomic fetch outcome.
FetchResult fetch(const net::HttpClient &client, std::string_view url);
} // namespace kafel::weather
