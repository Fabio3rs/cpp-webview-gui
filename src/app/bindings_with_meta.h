#pragma once

#include "app/bindings.h"
#include "app/bindings_meta.h"
#include "app/binary_rpc_bindings.h"

namespace app::bindings {

template <typename F>
void bind_typed_with_meta(
    webview::webview &w, const std::string &name, F &&func,
    std::source_location begin = std::source_location::current(),
    std::source_location end = std::source_location::current()) {
    bind_typed(w, name, std::forward<F>(func));
    // registra metadados para geração de .d.ts e índice (começo/fim)
    meta::register_binding_meta<F>(name, begin, end);
}

} // namespace app::bindings

namespace app::bindings {

template <typename F>
void bind_typed_with_wire_meta(
    webview::webview &w, binary_rpc::Dispatcher *binary, const std::string &name,
    F &&func, std::source_location begin = std::source_location::current(),
    std::source_location end = std::source_location::current()) {
    using Callable = std::decay_t<F>;
    Callable callable(std::forward<F>(func));
    bind_typed_with_meta(w, name, callable, begin, end);
    if (binary) {
        binary_rpc::bind_wire(*binary, name, callable);
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
            wv, rpc, jsName, (func), _bind_begin,                               \
            std::source_location::current());                                  \
    }
