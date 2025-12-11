#pragma once

#include "app/bindings.h"
#include "app/bindings_meta.h"

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

// Backwards-compatible macro: usual single-location form
#define APP_BIND_TYPED(wv, jsName, func)                                       \
    {                                                                          \
        constexpr auto _bind_begin = std::source_location::current();          \
        ::app::bindings::bind_typed_with_meta(                                 \
            wv, jsName, (func), _bind_begin, std::source_location::current()); \
    }
