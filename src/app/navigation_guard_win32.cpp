#include "app/navigation_guard.h"
#include "app/navigation_policy.h"

#if defined(_WIN32)
#include "webview/detail/utility/string.hh"

#include <WebView2.h>
#include <shellapi.h>
#include <wrl.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace app {
namespace {

struct GuardState {
    NavigationSession session;
};

std::string from_utf16(LPWSTR value) {
    if (!value) {
        return {};
    }
    const std::wstring native(value);
    CoTaskMemFree(value);
    return webview::detail::narrow_string(native);
}

NavigationDecision decide(GuardState &state, std::string_view url,
                          bool new_window, bool user_gesture) {
    // The current SDK does not distinguish an anchor click from script
    // navigation here. Only user-initiated new windows open externally.
    return state.session.decide(url, true, new_window, user_gesture,
                                new_window);
}

HRESULT on_navigation(const std::shared_ptr<GuardState> &state,
                      ICoreWebView2NavigationStartingEventArgs *args,
                      bool frame) {
    LPWSTR raw_uri = nullptr;
    if (FAILED(args->get_Uri(&raw_uri)) || !raw_uri) {
        return args->put_Cancel(TRUE);
    }
    const auto url = from_utf16(raw_uri);
    const auto verdict =
        state->session.decide(url, !frame, false, false, false);
    return args->put_Cancel(verdict != NavigationDecision::allow);
}

HRESULT on_new_window(const std::shared_ptr<GuardState> &state,
                      ICoreWebView2NewWindowRequestedEventArgs *args) {
    LPWSTR raw_uri = nullptr;
    BOOL user_initiated = FALSE;
    if (FAILED(args->get_Uri(&raw_uri)) || !raw_uri) {
        return args->put_Handled(TRUE);
    }
    const std::wstring native_uri(raw_uri);
    const auto url = from_utf16(raw_uri);
    const auto handled = args->put_Handled(TRUE);
    if (SUCCEEDED(args->get_IsUserInitiated(&user_initiated)) &&
        decide(*state, url, true, user_initiated != FALSE) ==
            NavigationDecision::open_external) {
        ShellExecuteW(nullptr, L"open", native_uri.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
    return handled;
}

class NavigationHandler final
    : public ICoreWebView2NavigationStartingEventHandler {
  public:
    NavigationHandler(std::shared_ptr<GuardState> state, bool frame)
        : state_(std::move(state)), frame_(frame) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void **result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_ICoreWebView2NavigationStartingEventHandler)) {
            *result =
                static_cast<ICoreWebView2NavigationStartingEventHandler *>(
                    this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --refs_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE
    Invoke(ICoreWebView2 *,
           ICoreWebView2NavigationStartingEventArgs *args) override {
        return on_navigation(state_, args, frame_);
    }

  private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<GuardState> state_;
    bool frame_;
};

class NewWindowHandler final
    : public ICoreWebView2NewWindowRequestedEventHandler {
  public:
    explicit NewWindowHandler(std::shared_ptr<GuardState> state)
        : state_(std::move(state)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void **result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_ICoreWebView2NewWindowRequestedEventHandler)) {
            *result =
                static_cast<ICoreWebView2NewWindowRequestedEventHandler *>(
                    this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --refs_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE
    Invoke(ICoreWebView2 *,
           ICoreWebView2NewWindowRequestedEventArgs *args) override {
        return on_new_window(state_, args);
    }

  private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<GuardState> state_;
};

} // namespace

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
    auto *controller = static_cast<ICoreWebView2Controller *>(handle.value());
    Microsoft::WRL::ComPtr<ICoreWebView2> browser;
    if (FAILED(controller->get_CoreWebView2(browser.GetAddressOf())) ||
        !browser) {
        return false;
    }
    auto state = std::make_shared<GuardState>(
        GuardState{NavigationSession(std::move(origin))});
    Microsoft::WRL::ComPtr<NavigationHandler> navigation;
    navigation.Attach(new NavigationHandler(state, false));
    Microsoft::WRL::ComPtr<NavigationHandler> frame_navigation;
    frame_navigation.Attach(new NavigationHandler(state, true));
    Microsoft::WRL::ComPtr<NewWindowHandler> new_window;
    new_window.Attach(new NewWindowHandler(std::move(state)));
    EventRegistrationToken token{};
    if (FAILED(browser->add_NavigationStarting(navigation.Get(), &token)) ||
        FAILED(browser->add_FrameNavigationStarting(frame_navigation.Get(),
                                                    &token)) ||
        FAILED(browser->add_NewWindowRequested(new_window.Get(), &token))) {
        return false;
    }
    return true;
}

} // namespace app
#endif
