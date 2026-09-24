#pragma once

#include "app/handlers.h"
#include "app/native_window_bindings.h"

namespace app {

// Edit this function to expose application methods. It is used by both the
// runtime and the build-time JS/TypeScript generator.
inline void register_app_bindings(webview::webview *view,
                                  binary_rpc::Dispatcher *dispatcher,
                                  const HandlerRegistry &handlers,
                                  WindowManager *windows) {
    setup(view, handlers, dispatcher);
    register_native_window_bindings(view, dispatcher, windows);
}

} // namespace app
