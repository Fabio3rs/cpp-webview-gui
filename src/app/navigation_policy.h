#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace app {

inline constexpr unsigned char ascii_delete = 127;
inline constexpr std::uint16_t http_default_port = 80;
inline constexpr std::uint16_t https_default_port = 443;

struct TrustedOrigin {
    std::string scheme;
    std::string host;
    std::uint16_t port = 0;

    bool operator==(const TrustedOrigin &) const = default;
};

// Only hierarchical web and application URLs can identify a privileged page.
[[nodiscard]] inline std::optional<TrustedOrigin>
parse_trusted_origin(std::string_view url) {
    for (const char character : url) {
        const auto c = static_cast<unsigned char>(character);
        if (c <= ' ' || c == '\\' || c == ascii_delete) {
            return std::nullopt;
        }
    }
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::nullopt;
    }
    TrustedOrigin result;
    result.scheme = url.substr(0, scheme_end);
    for (char &c : result.scheme) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (result.scheme != "http" && result.scheme != "https" &&
        result.scheme != "app-rpc") {
        return std::nullopt;
    }
    const auto authority_start = scheme_end + 3;
    const auto authority_end = url.find_first_of("/?#", authority_start);
    const auto authority =
        url.substr(authority_start, authority_end - authority_start);
    if (authority.empty() ||
        authority.find_first_of("@%") != std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view host = authority;
    std::string_view port_text;
    if (authority.front() == '[') {
        const auto bracket = authority.find(']');
        if (bracket == std::string_view::npos) {
            return std::nullopt;
        }
        host = authority.substr(0, bracket + 1);
        if (bracket + 1 < authority.size()) {
            if (authority[bracket + 1] != ':') {
                return std::nullopt;
            }
            port_text = authority.substr(bracket + 2);
        }
    } else if (const auto colon = authority.find(':');
               colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        port_text = authority.substr(colon + 1);
    }
    if (host.empty() ||
        (host.find(':') != std::string_view::npos && host.front() != '[')) {
        return std::nullopt;
    }
    result.host = host;
    for (char &c : result.host) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    result.port = result.scheme == "http"    ? http_default_port
                  : result.scheme == "https" ? https_default_port
                                             : 0;
    if (authority.size() != host.size()) {
        if (port_text.empty()) {
            return std::nullopt;
        }
        unsigned int parsed = 0;
        const auto converted = std::from_chars(
            port_text.data(), port_text.data() + port_text.size(), parsed);
        if (converted.ec != std::errc{} ||
            converted.ptr != port_text.data() + port_text.size() ||
            parsed == 0 ||
            parsed > (std::numeric_limits<std::uint16_t>::max)()) {
            return std::nullopt;
        }
        result.port = static_cast<std::uint16_t>(parsed);
    }
    return result;
}

[[nodiscard]] inline bool is_trusted_navigation(std::string_view target_url,
                                                const TrustedOrigin &trusted) {
    return parse_trusted_origin(target_url) == trusted;
}

enum class NavigationDecision { allow, deny, open_external };

[[nodiscard]] inline NavigationDecision
decide_navigation(std::string_view target_url, const TrustedOrigin &trusted,
                  bool new_window, bool user_gesture, bool link_clicked) {
    if (is_trusted_navigation(target_url, trusted)) {
        return new_window ? NavigationDecision::deny
                          : NavigationDecision::allow;
    }
    const auto external = parse_trusted_origin(target_url);
    if (user_gesture && link_clicked && external &&
        (external->scheme == "http" || external->scheme == "https")) {
        return NavigationDecision::open_external;
    }
    return NavigationDecision::deny;
}

} // namespace app
