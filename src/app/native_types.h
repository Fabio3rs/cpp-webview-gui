#pragma once

#include "app/bindings_meta.h"
#include "app/wire_codec.h"
#include "app/wire_js_emitter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace app {

// Data owned by JavaScript. Native code may forward it without parsing it.
struct OpaqueValue {
    // Binary calls hold bytes. Legacy WebView bindings keep their parsed JSON.
    static constexpr std::uint8_t cbor_null = 0xf6;
    static constexpr std::uint8_t cbor_empty_map = 0xa0;
    std::variant<binary_rpc::Bytes, bindings::json> storage;

    OpaqueValue() : storage(binary_rpc::Bytes{cbor_null}) {}
    explicit OpaqueValue(binary_rpc::Bytes bytes) : storage(std::move(bytes)) {}
    explicit OpaqueValue(bindings::json value) : storage(std::move(value)) {}
};

struct VersionInfo {
    std::string version;
};

struct FileOpenInfo {
    std::string path;
    std::string status;
};

struct ConfigInfo {
    std::string theme;
    std::string lang;
};

struct NativeWindowInfo {
    std::string id;
    std::string title;
};

// Fixed window settings are typed; extras contain Dockview's variable layout
// and panel state, which the native window manager never inspects.
struct WindowBootstrap {
    std::optional<std::string> window_id;
    std::optional<std::string> title;
    std::optional<std::string> url;
    std::optional<double> width;
    std::optional<double> height;
    std::optional<double> left;
    std::optional<double> top;
    OpaqueValue extras{binary_rpc::Bytes{OpaqueValue::cbor_empty_map}};
};

struct DropPoint {
    double x = 0;
    double y = 0;
};

struct OutsideDrop {
    OpaqueValue payload;
    std::optional<DropPoint> drop;
};

} // namespace app

namespace app::bindings {

template <> struct JsConv<app::OpaqueValue> {
    static app::OpaqueValue from_json(const json &value) {
        return app::OpaqueValue{value};
    }
    static json to_json(const app::OpaqueValue &value) {
        if (const auto *legacy = std::get_if<json>(&value.storage)) {
            return *legacy;
        }
        return json::from_cbor(std::get<binary_rpc::Bytes>(value.storage));
    }
};

template <> struct JsConv<app::VersionInfo> {
    static app::VersionInfo from_json(const json &value) {
        return {value.at("version").get<std::string>()};
    }
    static json to_json(const app::VersionInfo &value) {
        return {{"version", value.version}};
    }
};

template <> struct JsConv<app::FileOpenInfo> {
    static app::FileOpenInfo from_json(const json &value) {
        return {value.at("path").get<std::string>(),
                value.at("status").get<std::string>()};
    }
    static json to_json(const app::FileOpenInfo &value) {
        return {{"path", value.path}, {"status", value.status}};
    }
};

template <> struct JsConv<app::ConfigInfo> {
    static app::ConfigInfo from_json(const json &value) {
        return {value.at("theme").get<std::string>(),
                value.at("lang").get<std::string>()};
    }
    static json to_json(const app::ConfigInfo &value) {
        return {{"theme", value.theme}, {"lang", value.lang}};
    }
};

template <> struct JsConv<app::NativeWindowInfo> {
    static app::NativeWindowInfo from_json(const json &value) {
        return {value.at("id").get<std::string>(),
                value.at("title").get<std::string>()};
    }
    static json to_json(const app::NativeWindowInfo &value) {
        return {{"id", value.id}, {"title", value.title}};
    }
};

template <> struct JsConv<app::WindowBootstrap> {
    static app::WindowBootstrap from_json(const json &value) {
        if (!value.is_object()) {
            return {};
        }
        app::WindowBootstrap out;
        auto extras = value;
        auto read_string = [&](const char *key,
                               std::optional<std::string> &field) {
            if (auto it = value.find(key);
                it != value.end() && it->is_string()) {
                field = it->get<std::string>();
                extras.erase(key);
            }
        };
        auto read_number = [&](const char *key, std::optional<double> &field) {
            if (auto it = value.find(key);
                it != value.end() && it->is_number()) {
                field = it->get<double>();
                extras.erase(key);
            }
        };
        read_string("windowId", out.window_id);
        read_string("title", out.title);
        read_string("url", out.url);
        read_number("width", out.width);
        read_number("height", out.height);
        read_number("left", out.left);
        read_number("top", out.top);
        out.extras = JsConv<app::OpaqueValue>::from_json(extras);
        return out;
    }

    static json to_json(const app::WindowBootstrap &value) {
        json out = JsConv<app::OpaqueValue>::to_json(value.extras);
        if (!out.is_object()) {
            out = json::object();
        }
        if (value.window_id)
            out["windowId"] = *value.window_id;
        if (value.title)
            out["title"] = *value.title;
        if (value.url)
            out["url"] = *value.url;
        if (value.width)
            out["width"] = *value.width;
        if (value.height)
            out["height"] = *value.height;
        if (value.left)
            out["left"] = *value.left;
        if (value.top)
            out["top"] = *value.top;
        return out;
    }
};

template <> struct JsConv<app::DropPoint> {
    static app::DropPoint from_json(const json &value) {
        return {value.at("x").get<double>(), value.at("y").get<double>()};
    }
    static json to_json(const app::DropPoint &value) {
        return {{"x", value.x}, {"y", value.y}};
    }
};

template <> struct JsConv<app::OutsideDrop> {
    static app::OutsideDrop from_json(const json &value) {
        app::OutsideDrop out{
            JsConv<app::OpaqueValue>::from_json(value.at("payload")),
            std::nullopt};
        if (auto it = value.find("drop"); it != value.end()) {
            out.drop = JsConv<app::DropPoint>::from_json(*it);
        }
        return out;
    }
    static json to_json(const app::OutsideDrop &value) {
        json out = {
            {"payload", JsConv<app::OpaqueValue>::to_json(value.payload)}};
        if (value.drop) {
            out["drop"] = JsConv<app::DropPoint>::to_json(*value.drop);
        }
        return out;
    }
};

} // namespace app::bindings

namespace app::bindings::meta {

template <> struct TsType<app::OpaqueValue> {
    static std::string name() { return "unknown"; }
};
template <> struct TsType<app::WindowBootstrap> {
    static std::string name() {
        return "{ [key: string]: unknown; windowId?: string; title?: string; "
               "url?: string; width?: number; height?: number; left?: number; "
               "top?: number }";
    }
};
template <> struct TsType<app::OutsideDrop> {
    static std::string name() {
        return "{ payload: unknown; drop?: { x: number; y: number } }";
    }
};

} // namespace app::bindings::meta

namespace app::binary_rpc {

template <> struct WireCodec<app::OpaqueValue> {
    static app::OpaqueValue read(Reader &reader) {
        const auto view = reader.bytes();
        return app::OpaqueValue{Bytes(view.begin(), view.end())};
    }
    static void write(Writer &writer, const app::OpaqueValue &value) {
        if (const auto *bytes = std::get_if<Bytes>(&value.storage)) {
            writer.bytes(*bytes);
        } else {
            writer.bytes(bindings::json::to_cbor(
                std::get<bindings::json>(value.storage)));
        }
    }
};

template <> struct WireFields<app::VersionInfo> {
    static constexpr auto fields() {
        return std::make_tuple(
            wire_field("version", &app::VersionInfo::version));
    }
};

template <> struct WireFields<app::FileOpenInfo> {
    static constexpr auto fields() {
        return std::make_tuple(
            wire_field("path", &app::FileOpenInfo::path),
            wire_field("status", &app::FileOpenInfo::status));
    }
};

template <> struct WireFields<app::ConfigInfo> {
    static constexpr auto fields() {
        return std::make_tuple(wire_field("theme", &app::ConfigInfo::theme),
                               wire_field("lang", &app::ConfigInfo::lang));
    }
};

template <> struct WireFields<app::NativeWindowInfo> {
    static constexpr auto fields() {
        return std::make_tuple(
            wire_field("id", &app::NativeWindowInfo::id),
            wire_field("title", &app::NativeWindowInfo::title));
    }
};

template <> struct WireCodec<app::WindowBootstrap> {
    static app::WindowBootstrap read(Reader &reader) {
        app::WindowBootstrap out;
        out.window_id = WireCodec<std::optional<std::string>>::read(reader);
        out.title = WireCodec<std::optional<std::string>>::read(reader);
        out.url = WireCodec<std::optional<std::string>>::read(reader);
        out.width = WireCodec<std::optional<double>>::read(reader);
        out.height = WireCodec<std::optional<double>>::read(reader);
        out.left = WireCodec<std::optional<double>>::read(reader);
        out.top = WireCodec<std::optional<double>>::read(reader);
        out.extras = WireCodec<app::OpaqueValue>::read(reader);
        return out;
    }
    static void write(Writer &writer, const app::WindowBootstrap &value) {
        WireCodec<std::optional<std::string>>::write(writer, value.window_id);
        WireCodec<std::optional<std::string>>::write(writer, value.title);
        WireCodec<std::optional<std::string>>::write(writer, value.url);
        WireCodec<std::optional<double>>::write(writer, value.width);
        WireCodec<std::optional<double>>::write(writer, value.height);
        WireCodec<std::optional<double>>::write(writer, value.left);
        WireCodec<std::optional<double>>::write(writer, value.top);
        WireCodec<app::OpaqueValue>::write(writer, value.extras);
    }
};

template <> struct WireFields<app::DropPoint> {
    static constexpr auto fields() {
        return std::make_tuple(wire_field("x", &app::DropPoint::x),
                               wire_field("y", &app::DropPoint::y));
    }
};

template <> struct WireFields<app::OutsideDrop> {
    static constexpr auto fields() {
        return std::make_tuple(
            wire_field("payload", &app::OutsideDrop::payload),
            wire_field("drop", &app::OutsideDrop::drop));
    }
};

template <> struct JsWire<app::OpaqueValue> {
    static std::string write(std::string_view writer, std::string_view value) {
        return "writeOpaque(" + std::string(writer) + ", " +
               std::string(value) + ");";
    }
    static std::string read(std::string_view reader) {
        return "readOpaque(" + std::string(reader) + ")";
    }
};

template <> struct JsWire<app::WindowBootstrap> {
    static std::string write(std::string_view writer, std::string_view value) {
        return "writeBootstrap(" + std::string(writer) + ", " +
               std::string(value) + ");";
    }
    static std::string read(std::string_view reader) {
        return "readBootstrap(" + std::string(reader) + ")";
    }
};

template <> struct JsWire<app::OutsideDrop> {
    static std::string read(std::string_view reader) {
        return "readOutsideDrop(" + std::string(reader) + ")";
    }
};

} // namespace app::binary_rpc
