#pragma once

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace app::dev_ports {

inline int parse(std::string_view value, std::string_view name) {
    int port = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() ||
        port < 1 || port > 65535) {
        throw std::invalid_argument(std::string(name) +
                                    " must be a port between 1 and 65535");
    }
    return port;
}

inline int from_environment(const char *name, int fallback) {
    const char *value = std::getenv(name);
    return value ? parse(value, name) : fallback;
}

struct Config {
    int vite = 5173;
    int rpc = 5174;
};

inline Config get() {
    Config config{from_environment("APP_VITE_PORT", 5173),
                  from_environment("APP_RPC_PORT", 5174)};
    if (config.vite == config.rpc) {
        throw std::invalid_argument(
            "APP_VITE_PORT and APP_RPC_PORT must differ");
    }
    return config;
}

inline std::string vite_origin(const Config &config) {
    return "http://127.0.0.1:" + std::to_string(config.vite);
}

} // namespace app::dev_ports
