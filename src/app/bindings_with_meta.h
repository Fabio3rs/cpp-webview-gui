#pragma once

#include "app/binary_rpc_bindings.h"
#include "app/bindings.h"
#include "app/bindings_meta.h"
#include "app/wire_js_emitter.h"

#include <stdexcept>

namespace app::bindings {

template <typename F>
void bind_typed_with_meta(
    webview::webview &w, const std::string &name, F &&func,
    std::source_location begin = std::source_location::current(),
    std::source_location end = std::source_location::current()) {
    bind_typed(w, name, std::forward<F>(func));
    // Metadata is collected only by the build-time emitter.
#if defined(APP_BINDINGS_EMITTER)
    meta::register_binding_meta<F>(name, begin, end);
#else
    static_cast<void>(begin);
    static_cast<void>(end);
#endif
}

} // namespace app::bindings

namespace app::bindings {

template <typename F>
void bind_typed_with_wire_meta(
    webview::webview *w, binary_rpc::Dispatcher *binary,
    const std::string &name, F &&func,
    std::source_location begin = std::source_location::current(),
    std::source_location end = std::source_location::current()) {
    using Callable = std::decay_t<F>;
    Callable callable(std::forward<F>(func));
#if defined(APP_BINDINGS_EMITTER)
    meta::register_binding_meta<Callable>(
        name, begin, end,
        binary_rpc::emit_wire_binding<Callable>(binary_rpc::method_id(name)));
#else
    static_cast<void>(begin);
    static_cast<void>(end);
#endif
    if (binary) {
        binary_rpc::bind_wire(*binary, name, callable);
    } else if (w) {
        if constexpr (binary_rpc::has_borrowed_input_v<Callable>) {
            throw std::logic_error("BinaryView requires binary RPC transport");
        } else {
            bind_typed(*w, name, callable);
        }
    }
}

} // namespace app::bindings

// Backwards-compatible macro: usual single-location form
// The macro captures the caller's source location for generated bindings.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define APP_BIND_TYPED(wv, jsName, func)                                       \
    {                                                                          \
        constexpr auto _bind_begin = std::source_location::current();          \
        ::app::bindings::bind_typed_with_meta(                                 \
            wv, jsName, (func), _bind_begin, std::source_location::current()); \
    }

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define APP_BIND_TYPED_WIRE(wv, rpc, jsName, func)                             \
    {                                                                          \
        constexpr auto _bind_begin = std::source_location::current();          \
        ::app::bindings::bind_typed_with_wire_meta(                            \
            wv, rpc, jsName, (func), _bind_begin,                              \
            std::source_location::current());                                  \
    }
