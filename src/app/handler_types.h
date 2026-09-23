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

namespace app::bindings::meta {

template <> struct TsType<app::PingResult> {
    static std::string name() {
        return "{ message: string; echo: string }";
    }
};

} // namespace app::bindings::meta

namespace app::binary_rpc {

template <> struct WireCodec<app::PingResult> {
    static app::PingResult read(Reader &reader) {
        return {reader.string(), reader.string()};
    }
    static void write(Writer &writer, const app::PingResult &value) {
        writer.string(value.message);
        writer.string(value.echo);
    }
};

} // namespace app::binary_rpc
