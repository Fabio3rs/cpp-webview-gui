#pragma once

#include "app/handlers.h"
#include "app/window_manager.h"

namespace app {

// Framework bindings for native window management.
inline void register_native_window_bindings(webview::webview *view,
                                            binary_rpc::Dispatcher *dispatcher,
                                            WindowManager *windows) {
    APP_BIND_TYPED_WIRE(view, dispatcher, "createNativeWindow",
                        [windows](WindowBootstrap bootstrap) {
                            return windows->create_window(bootstrap);
                        });
    APP_BIND_TYPED_WIRE(view, dispatcher, "getBootstrap",
                        [windows](const std::string &window_id) {
                            auto bootstrap = windows->take_bootstrap(window_id);
                            if (!bootstrap) {
                                throw bindings::BindingError(
                                    "Bootstrap not found",
                                    bindings::ErrorCode::MissingArg);
                            }
                            return *bootstrap;
                        });
    APP_BIND_TYPED_WIRE(
        view, dispatcher, "postNativeEvent",
        [windows](const std::string &window_id, OpaqueValue event) {
            if (!windows->post_opaque_event(window_id, event)) {
                throw bindings::BindingError("Window not found",
                                             bindings::ErrorCode::MissingArg);
            }
        });
    APP_BIND_TYPED_WIRE(view, dispatcher, "closeNativeWindow",
                        [windows](const std::string &window_id) {
                            if (!windows->close_window(window_id)) {
                                throw bindings::BindingError(
                                    "Window not found",
                                    bindings::ErrorCode::MissingArg);
                            }
                        });
    APP_BIND_TYPED_WIRE(view, dispatcher, "listNativeWindows",
                        [windows]() { return windows->list_windows(); });
    APP_BIND_TYPED_WIRE(
        view, dispatcher, "startNativeDrag",
        [windows](const std::string &window_id, OpaqueValue payload) {
            windows->start_drag_tracking(window_id, payload);
        });
    APP_BIND_TYPED_WIRE(view, dispatcher, "completeNativeDrag",
                        [windows](const std::string &target_window_id) {
                            return windows->complete_drag_tracking(
                                target_window_id);
                        });
    APP_BIND_TYPED_WIRE(view, dispatcher, "stopNativeDrag",
                        [windows]() { windows->stop_drag_tracking(); });
    APP_BIND_TYPED_WIRE(view, dispatcher, "completeNativeDragOutside",
                        [windows](const std::string &window_id) {
                            return windows->complete_drag_outside(window_id);
                        });
}

} // namespace app
