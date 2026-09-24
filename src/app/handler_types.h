#pragma once

#include "app/bindings_meta.h"
#include "app/wire_codec.h"

#include <string>

namespace app {

struct PingResult {
    std::string message;
    std::string echo;
};

} // namespace app

namespace app::bindings {

template <> struct JsConv<app::PingResult> {
    static app::PingResult from_json(const json &value) {
        return {value.at("message").get<std::string>(),
                value.at("echo").get<std::string>()};
    }
    static json to_json(const app::PingResult &value) {
        return {{"message", value.message}, {"echo", value.echo}};
    }
};

} // namespace app::bindings

namespace app::binary_rpc {

template <> struct WireFields<app::PingResult> {
    static constexpr auto fields() {
        return std::make_tuple(wire_field("message", &app::PingResult::message),
                               wire_field("echo", &app::PingResult::echo));
    }
};

} // namespace app::binary_rpc
