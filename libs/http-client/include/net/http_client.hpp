#pragma once
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace net {
struct HttpOptions {
    std::chrono::milliseconds timeout{10000};
    std::size_t max_body_bytes = 64 * 1024;
};
struct HttpResponse {
    int status;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};
enum class HttpErrorCode {
    InvalidArgument,
    Configuration,
    Connection,
    Tls,
    Timeout,
    ResponseTooLarge,
    Protocol,
    Network
};
struct HttpError {
    HttpErrorCode code;
    std::string message;
};
class HttpResult {
  public:
    HttpResult(HttpResponse response) : value_(std::move(response)) {}
    HttpResult(HttpError error) : value_(std::move(error)) {}
    explicit operator bool() const noexcept { return std::holds_alternative<HttpResponse>(value_); }
    const HttpResponse &operator*() const { return std::get<HttpResponse>(value_); }
    const HttpResponse *operator->() const { return &std::get<HttpResponse>(value_); }
    const HttpError &error() const { return std::get<HttpError>(value_); }

  private:
    std::variant<HttpResponse, HttpError> value_;
};
// Synchronous GET. Each call owns its transport; no persistent connections or
// retry policy. Concurrent calls are independent. TLS verification is mandatory.
class HttpClient {
  public:
    // Empty selects the platform CA bundle; explicit paths are useful in tests.
    explicit HttpClient(std::string ca_bundle_path = {}) : ca_bundle_path_(std::move(ca_bundle_path)) {}
    HttpResult get(std::string_view url, HttpOptions options = {}) const;

  private:
    std::string ca_bundle_path_;
};
std::string_view error_name(HttpErrorCode code) noexcept;
} // namespace net
