#pragma once

#include "app/native_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace app {

// The tag is part of the native-event wire format. Keep it in sync with
// readNativeEvent() in ui/src/native_wire_types.js.
enum class NativeEventKind : std::uint8_t {
    forwarded = 0,
    window_closed = 1,
    window_error = 2,
    drag_leave = 3,
    drag_hover = 4,
    drag_complete = 5
};

struct NativeEvent {
    NativeEventKind kind;
    std::string window_id;
    std::string target_window_id;
    std::string message;
    std::optional<OpaqueValue> payload;

    [[nodiscard]] static NativeEvent forwarded(OpaqueValue value) {
        return {NativeEventKind::forwarded, {}, {}, {}, std::move(value)};
    }

    [[nodiscard]] static NativeEvent window_closed(std::string id) {
        return {NativeEventKind::window_closed, std::move(id), {}, {}, {}};
    }

    [[nodiscard]] static NativeEvent window_error(std::string id,
                                                  std::string error) {
        return {NativeEventKind::window_error,
                std::move(id),
                {},
                std::move(error),
                {}};
    }

    [[nodiscard]] static NativeEvent drag_leave(std::string origin_id) {
        return {NativeEventKind::drag_leave, std::move(origin_id), {}, {}, {}};
    }

    [[nodiscard]] static NativeEvent drag_hover(std::string origin_id) {
        return {NativeEventKind::drag_hover, std::move(origin_id), {}, {}, {}};
    }

    [[nodiscard]] static NativeEvent drag_complete(std::string origin_id,
                                                   std::string target_id,
                                                   OpaqueValue value) {
        return {NativeEventKind::drag_complete,
                std::move(origin_id),
                std::move(target_id),
                {},
                std::move(value)};
    }
};

[[nodiscard]] inline binary_rpc::Bytes
encode_native_event(const NativeEvent &event) {
    binary_rpc::Writer writer;
    writer.u8(static_cast<std::uint8_t>(event.kind));
    switch (event.kind) {
    case NativeEventKind::forwarded:
        binary_rpc::WireCodec<OpaqueValue>::write(writer,
                                                  event.payload.value());
        break;
    case NativeEventKind::window_closed:
    case NativeEventKind::drag_leave:
    case NativeEventKind::drag_hover:
        writer.string(event.window_id);
        break;
    case NativeEventKind::window_error:
        writer.string(event.window_id);
        writer.string(event.message);
        break;
    case NativeEventKind::drag_complete:
        writer.string(event.window_id);
        writer.string(event.target_window_id);
        binary_rpc::WireCodec<OpaqueValue>::write(writer,
                                                  event.payload.value());
        break;
    }
    return std::move(writer).take();
}

// Only the development bridge needs a JSON object for WebView's old bind API.
[[nodiscard]] inline bindings::json
legacy_native_event(const NativeEvent &event) {
    using bindings::json;
    switch (event.kind) {
    case NativeEventKind::forwarded:
        return bindings::JsConv<OpaqueValue>::to_json(event.payload.value());
    case NativeEventKind::window_closed:
        return {{"type", "native-window.closed"},
                {"windowId", event.window_id}};
    case NativeEventKind::window_error:
        return {{"type", "native-window.error"},
                {"windowId", event.window_id},
                {"message", event.message}};
    case NativeEventKind::drag_leave:
    case NativeEventKind::drag_hover:
        return {{"type", event.kind == NativeEventKind::drag_leave
                             ? "dock.dragLeave"
                             : "dock.dragHover"},
                {"payload", {{"originWindowId", event.window_id}}}};
    case NativeEventKind::drag_complete:
        return {{"type", "dock.dragComplete"},
                {"payload",
                 {{"originWindowId", event.window_id},
                  {"targetWindowId", event.target_window_id},
                  {"dragPayload", bindings::JsConv<OpaqueValue>::to_json(
                                      event.payload.value())}}}};
    }
    throw binary_rpc::WireError("Unknown native event kind");
}

} // namespace app
