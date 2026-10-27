#include "net/http_client.hpp"
#include "httplib.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>

namespace net {
namespace {
using Code = HttpErrorCode;
Code classify(httplib::Error error) {
    using E = httplib::Error;
    switch (error) {
    case E::Connection:
    case E::ConnectionClosed:
    case E::BindIPAddress:
        return Code::Connection;
    case E::SSLLoadingCerts:
        return Code::Configuration;
    case E::SSLConnection:
    case E::SSLServerVerification:
    case E::SSLServerHostnameVerification:
        return Code::Tls;
    case E::Timeout:
    case E::ConnectionTimeout:
        return Code::Timeout;
    case E::ExceedMaxPayloadSize:
        return Code::ResponseTooLarge;
    case E::HTTPParsing:
    case E::InvalidHTTPVersion:
    case E::InvalidHeaders:
    case E::UnsupportedContentEncoding:
        return Code::Protocol;
    default:
        return Code::Network;
    }
}
const char *default_ca() {
#ifdef __APPLE__
    return "/etc/ssl/cert.pem";
#else
    return "/etc/ssl/certs/ca-certificates.crt";
#endif
}
} // namespace
HttpResult HttpClient::get(std::string_view input, HttpOptions options) const {
    if (input.empty() || input.size() > 8192 || options.timeout.count() <= 0 ||
        options.timeout > std::chrono::hours(24) || options.max_body_bytes == 0)
        return HttpError{Code::InvalidArgument, "Invalid URL, timeout or response limit"};
    for (unsigned char c : input)
        if (c <= 32 || c == 127)
            return HttpError{Code::InvalidArgument, "URL contains whitespace or control characters"};
    const std::string url(input);
    // Parsing stays inside the pinned backend adapter, never in the public API.
    httplib::detail::UrlComponents parts;
    if (!httplib::detail::parse_url(url, parts) || (parts.scheme != "https" && parts.scheme != "http") ||
        parts.host.empty() || parts.host.find('@') != std::string::npos)
        return HttpError{Code::InvalidArgument, "Expected absolute HTTP(S) URL without credentials"};
    int port = parts.scheme == "https" ? 443 : 80;
    if (!parts.port.empty() && !httplib::detail::parse_port(parts.port, port))
        return HttpError{Code::InvalidArgument, "Invalid port"};
    const auto host = parts.host.find(':') == std::string::npos ? parts.host : "[" + parts.host + "]";
    const std::string origin = parts.scheme + "://" + host + ":" + std::to_string(port);
    const std::string target = (parts.path.empty() ? "/" : parts.path) + parts.query;
    const auto ca = ca_bundle_path_.empty() ? std::string(default_ca()) : ca_bundle_path_;
    if (parts.scheme == "https" && !std::ifstream(ca).good())
        return HttpError{Code::Configuration, "Cannot read CA bundle"};
    httplib::Client client(origin);
    if (!client.is_valid())
        return HttpError{Code::Configuration, "Cannot initialize HTTP transport"};
    if (parts.scheme == "https") {
        client.set_ca_cert_path(ca);
        client.enable_server_certificate_verification(true);
        client.enable_server_hostname_verification(true);
    }
    client.set_keep_alive(false);
    client.set_follow_location(false);
    client.set_decompress(false);
    client.set_path_encode(false);
    client.set_connection_timeout(std::chrono::seconds((options.timeout.count() + 999) / 1000));
    client.set_read_timeout(options.timeout);
    client.set_write_timeout(options.timeout);
    client.set_max_timeout(options.timeout);
    std::string body;
    bool too_large = false;
    const auto started = std::chrono::steady_clock::now();
    auto result = client.Get(
        target, {{"Accept-Encoding", "identity"}},
        [&](const httplib::Response &response) {
            const auto length = response.get_header_value("Content-Length");
            std::size_t size = 0;
            if (!length.empty()) {
                const auto parsed = std::from_chars(length.data(), length.data() + length.size(), size);
                if (parsed.ec == std::errc{} && parsed.ptr == length.data() + length.size() &&
                    size > options.max_body_bytes) {
                    too_large = true;
                    return false;
                }
            }
            return true;
        },
        [&](const char *data, std::size_t size) {
            if (size > options.max_body_bytes - body.size()) {
                too_large = true;
                return false;
            }
            body.append(data, size);
            return true;
        });
    if (too_large)
        return HttpError{Code::ResponseTooLarge, "Response exceeds configured body limit"};
    if (std::chrono::steady_clock::now() - started >= options.timeout)
        return HttpError{Code::Timeout, "Request deadline exceeded"};
    if (!result)
        return HttpError{classify(result.error()), httplib::to_string(result.error())};
    HttpResponse response{result->status, std::move(body), {}};
    response.headers.assign(result->headers.begin(), result->headers.end());
    return response;
}
std::string_view error_name(HttpErrorCode code) noexcept {
    switch (code) {
    case Code::InvalidArgument:
        return "invalid_argument";
    case Code::Configuration:
        return "configuration";
    case Code::Connection:
        return "connection";
    case Code::Tls:
        return "tls";
    case Code::Timeout:
        return "timeout";
    case Code::ResponseTooLarge:
        return "response_too_large";
    case Code::Protocol:
        return "protocol";
    case Code::Network:
        return "network";
    }
    return "unknown";
}
} // namespace net
