#include "app/navigation_policy.h"
#include "app/navigation_guard.h"

#if defined(__linux__)
#include <gtk/gtk.h>
#if GTK_MAJOR_VERSION >= 4
#include <webkit/webkit.h>
#else
#include <webkit2/webkit2.h>
#endif

#include <memory>

namespace app {
namespace {

struct GuardState {
    TrustedOrigin origin;
};

gboolean decide_policy(WebKitWebView *, WebKitPolicyDecision *decision,
                       WebKitPolicyDecisionType type, gpointer data) {
    auto &state = *static_cast<GuardState *>(data);
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        return FALSE;
    }
    auto *navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    auto *action =
        webkit_navigation_policy_decision_get_navigation_action(navigation);
    auto *request = webkit_navigation_action_get_request(action);
    const auto *uri = webkit_uri_request_get_uri(request);
    const std::string_view target = uri ? uri : "";
    const auto verdict =
        decide_navigation(target, state.origin,
                          type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION,
                          webkit_navigation_action_is_user_gesture(action),
                          webkit_navigation_action_get_navigation_type(
                              action) == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED);
    if (verdict == NavigationDecision::allow) {
        return FALSE;
    }
    const std::string external_uri =
        verdict == NavigationDecision::open_external ? std::string(target) : "";
    webkit_policy_decision_ignore(decision);
    if (verdict == NavigationDecision::open_external) {
        GError *error = nullptr;
        g_app_info_launch_default_for_uri(external_uri.c_str(), nullptr,
                                          &error);
        g_clear_error(&error);
    }
    return TRUE;
}

} // namespace

bool install_navigation_guard(webview::webview &window,
                              std::string_view trusted_url) {
    auto origin = parse_trusted_origin(trusted_url);
    if (!origin) {
        return false;
    }
    auto handle = window.browser_controller();
    if (!handle.ok()) {
        return false;
    }
    auto state = std::make_unique<GuardState>(std::move(*origin));
    g_signal_connect_data(
        WEBKIT_WEB_VIEW(handle.value()), "decide-policy",
        G_CALLBACK(decide_policy), state.release(),
        +[](gpointer data, GClosure *) {
            std::unique_ptr<GuardState> owner(static_cast<GuardState *>(data));
        },
        GConnectFlags(0));
    return true;
}

} // namespace app
#endif
