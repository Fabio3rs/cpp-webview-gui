#include "app/navigation_guard.h"
#include "app/navigation_policy.h"

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

#include <optional>
#include <string>
#include <utility>

namespace {

struct GuardState {
    app::NavigationSession session;
};

char navigation_guard_key;

} // namespace

@interface AppNavigationGuardDelegate : NSObject <WKNavigationDelegate> {
  @public
    GuardState *state_;
}
@end

@implementation AppNavigationGuardDelegate

- (void)dealloc {
    delete state_;
    [super dealloc];
}

- (void)webView:(WKWebView *)webView
    didFinishNavigation:(WKNavigation *)navigation {
    (void)webView;
    (void)navigation;
    state_->session.document_loaded();
}

- (void)webView:(WKWebView *)webView
    decidePolicyForNavigationAction:(WKNavigationAction *)action
                   decisionHandler:(void (^)(WKNavigationActionPolicy))handler {
    (void)webView;
    NSURL *target_url = action.request.URL;
    const char *utf8 = target_url.absoluteString.UTF8String;
    const std::string target = utf8 ? utf8 : "";
    const bool new_window = action.targetFrame == nil;
    const bool link_clicked =
        action.navigationType == WKNavigationTypeLinkActivated;
    const bool main_frame = !new_window && action.targetFrame.isMainFrame;
    const auto verdict = state_->session.decide(
        target, main_frame, new_window, link_clicked, link_clicked);
    handler(verdict == app::NavigationDecision::allow
                ? WKNavigationActionPolicyAllow
                : WKNavigationActionPolicyCancel);
    if (verdict == app::NavigationDecision::open_external) {
        [[NSWorkspace sharedWorkspace] openURL:target_url];
    }
}

@end

namespace app {

bool install_navigation_guard(webview::webview &window,
                              std::string_view trusted_url) {
    auto origin = trusted_url.empty() ? std::optional<TrustedOrigin>{}
                                      : parse_trusted_origin(trusted_url);
    if (!trusted_url.empty() && !origin) {
        return false;
    }
    auto handle = window.browser_controller();
    if (!handle.ok()) {
        return false;
    }
    auto *view = reinterpret_cast<WKWebView *>(handle.value());
    auto *delegate = [[AppNavigationGuardDelegate alloc] init];
    delegate->state_ = new GuardState{NavigationSession(std::move(origin))};
    objc_setAssociatedObject(view, &navigation_guard_key, delegate,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    view.navigationDelegate = delegate;
    [delegate release];
    return true;
}

} // namespace app
