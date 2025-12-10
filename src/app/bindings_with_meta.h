#pragma once

#include "app/bindings.h"
#include "app/bindings_meta.h"

namespace app::bindings {

template <typename F>
void bind_typed_with_meta(
    webview::webview &w, const std::string &name, F &&func,
    std::source_location loc = std::source_location::current()) {
    bind_typed(w, name, std::forward<F>(func));
    // registra metadados para geração de .d.ts e índice
    meta::register_binding_meta<F>(name, loc);
}

} // namespace app::bindings

#define APP_BIND_TYPED(wv, jsName, func)                                       \
    ::app::bindings::bind_typed_with_meta(wv, jsName, (func),                  \
                                          std::source_location::current())
