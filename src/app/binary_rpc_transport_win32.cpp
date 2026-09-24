#include "app/binary_rpc_transport.h"
#include "app/binding_error.h"

#include "webview/detail/utility/string.hh"

#include <WebView2.h>
#include <wrl.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace app::binary_rpc {
namespace {

constexpr std::string_view origin = "https://cpp-webview-gui.invalid";
constexpr std::string_view event_base =
    "https://cpp-webview-gui.invalid/rpc/event/";
constexpr std::size_t chunk_size = 8192;
constexpr std::size_t max_pending_events = 64;
constexpr std::size_t max_pending_event_bytes = std::size_t{32} * 1024 * 1024;

struct TransportState {
    Dispatcher dispatcher;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    std::map<std::uint64_t, Bytes> events;
    std::size_t pending_event_bytes = 0;
    std::uint64_t next_event_id = 1;
    std::string html;
};

// All WebViews in this application run on the UI apartment and share one RPC
// dispatcher. Event handlers retain the state until their WebView is destroyed.
std::shared_ptr<TransportState> active_state;

std::string from_utf16(LPWSTR value) {
    if (!value) {
        return {};
    }
    const std::wstring native(value);
    CoTaskMemFree(value);
    return webview::detail::narrow_string(native);
}

Microsoft::WRL::ComPtr<ICoreWebView2> browser_of(webview::webview &window) {
    Microsoft::WRL::ComPtr<ICoreWebView2> browser;
    const auto handle = window.browser_controller();
    if (handle.ok() && handle.value()) {
        auto *controller =
            static_cast<ICoreWebView2Controller *>(handle.value());
        controller->get_CoreWebView2(browser.GetAddressOf());
    }
    return browser;
}

Microsoft::WRL::ComPtr<IStream> stream_from(Bytes body) {
    Microsoft::WRL::ComPtr<IStream> stream;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, body.empty() ? 1 : body.size());
    if (!memory) {
        return stream;
    }
    if (!body.empty()) {
        void *data = GlobalLock(memory);
        if (!data) {
            GlobalFree(memory);
            return stream;
        }
        std::memcpy(data, body.data(), body.size());
        GlobalUnlock(memory);
    }
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, stream.GetAddressOf()))) {
        GlobalFree(memory);
    }
    return stream;
}

HRESULT respond(TransportState &state,
                ICoreWebView2WebResourceRequestedEventArgs *args, Bytes body,
                int status, const wchar_t *mime) {
    auto stream = stream_from(std::move(body));
    if (!stream) {
        return E_OUTOFMEMORY;
    }
    const std::wstring headers =
        std::wstring(L"Content-Type: ") + mime +
        L"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff";
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    const HRESULT created = state.environment->CreateWebResourceResponse(
        stream.Get(), status, status == 200 ? L"OK" : L"Error", headers.c_str(),
        response.GetAddressOf());
    return FAILED(created) ? created : args->put_Response(response.Get());
}

bool read_body(ICoreWebView2WebResourceRequest *request, Bytes &body) {
    Microsoft::WRL::ComPtr<IStream> stream;
    if (FAILED(request->get_Content(stream.GetAddressOf()))) {
        return false;
    }
    if (!stream) {
        return true;
    }
    std::array<std::uint8_t, chunk_size> chunk{};
    for (;;) {
        ULONG count = 0;
        const HRESULT result = stream->Read(
            chunk.data(), static_cast<ULONG>(chunk.size()), &count);
        if (FAILED(result) || count > max_message_size - body.size()) {
            return false;
        }
        body.insert(body.end(), chunk.begin(), chunk.begin() + count);
        if (count == 0 || result == S_FALSE) {
            return true;
        }
    }
}

HRESULT on_request(TransportState &state,
                   ICoreWebView2WebResourceRequestedEventArgs *args) {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
    if (FAILED(args->get_Request(request.GetAddressOf())) || !request) {
        return E_FAIL;
    }
    LPWSTR raw_uri = nullptr;
    LPWSTR raw_method = nullptr;
    if (FAILED(request->get_Uri(&raw_uri)) ||
        FAILED(request->get_Method(&raw_method))) {
        CoTaskMemFree(raw_uri);
        CoTaskMemFree(raw_method);
        return E_FAIL;
    }
    const auto uri = from_utf16(raw_uri);
    const auto method = from_utf16(raw_method);
    Microsoft::WRL::ComPtr<ICoreWebView2HttpRequestHeaders> headers;
    if (SUCCEEDED(request->get_Headers(headers.GetAddressOf())) && headers) {
        LPWSTR raw_origin = nullptr;
        if (SUCCEEDED(headers->GetHeader(L"Origin", &raw_origin))) {
            const auto caller = from_utf16(raw_origin);
            if (caller != origin) {
                return respond(state, args, {}, 403,
                               L"application/octet-stream");
            }
        }
    }
    if (uri == page_url && method == "GET") {
        return respond(state, args, Bytes(state.html.begin(), state.html.end()),
                       200, L"text/html; charset=utf-8");
    }
    if (method == "GET" && uri.starts_with(event_base)) {
        const auto text = std::string_view(uri).substr(event_base.size());
        std::uint64_t token = 0;
        const auto parsed =
            std::from_chars(text.data(), text.data() + text.size(), token);
        if (text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + text.size()) {
            return respond(state, args, {}, 404, L"application/octet-stream");
        }
        auto event = state.events.find(token);
        if (event == state.events.end()) {
            return respond(state, args, {}, 404, L"application/octet-stream");
        }
        Bytes body = std::move(event->second);
        state.pending_event_bytes -= body.size();
        state.events.erase(event);
        return respond(state, args, std::move(body), 200,
                       L"application/octet-stream");
    }
    if (method != "POST" || !uri.starts_with(rpc_base)) {
        return respond(state, args, {}, 404, L"application/octet-stream");
    }
    const auto id_text = std::string_view(uri).substr(rpc_base.size());
    std::uint32_t id = 0;
    const auto parsed =
        std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
    if (id_text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != id_text.data() + id_text.size()) {
        return respond(state, args, {}, 404, L"application/octet-stream");
    }
    Bytes body;
    if (!read_body(request.Get(), body)) {
        return respond(state, args, {}, 413, L"application/octet-stream");
    }
    try {
        return respond(state, args, state.dispatcher.call_enveloped(id, body),
                       200, L"application/octet-stream");
    } catch (const bindings::BindingError &error) {
        return respond(state, args,
                       error_response(static_cast<std::uint32_t>(error.code()),
                                      error.what()),
                       200, L"application/octet-stream");
    } catch (const WireError &error) {
        return respond(state, args,
                       error_response(static_cast<std::uint32_t>(
                                          bindings::ErrorCode::InvalidArgs),
                                      error.what()),
                       200, L"application/octet-stream");
    } catch (const std::exception &error) {
        return respond(state, args,
                       error_response(static_cast<std::uint32_t>(
                                          bindings::ErrorCode::InternalError),
                                      error.what()),
                       200, L"application/octet-stream");
    }
}

class RequestHandler final
    : public ICoreWebView2WebResourceRequestedEventHandler {
  public:
    explicit RequestHandler(std::shared_ptr<TransportState> state)
        : state_(std::move(state)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void **result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid,
                       IID_ICoreWebView2WebResourceRequestedEventHandler)) {
            *result =
                static_cast<ICoreWebView2WebResourceRequestedEventHandler *>(
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
           ICoreWebView2WebResourceRequestedEventArgs *args) override {
        return on_request(*state_, args);
    }

  private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<TransportState> state_;
};

bool attach(webview::webview &window,
            const std::shared_ptr<TransportState> &state) {
    auto browser = browser_of(window);
    if (!browser) {
        return false;
    }
    Microsoft::WRL::ComPtr<RequestHandler> handler;
    handler.Attach(new RequestHandler(state));
    EventRegistrationToken token{};
    const std::wstring filter =
        std::wstring(origin.begin(), origin.end()) + L"/*";
    return SUCCEEDED(browser->AddWebResourceRequestedFilter(
               filter.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL)) &&
           SUCCEEDED(browser->add_WebResourceRequested(handler.Get(), &token));
}

} // namespace

bool install_transport(webview::webview &window, Dispatcher dispatcher) {
    auto browser = browser_of(window);
    if (!browser) {
        return false;
    }
    Microsoft::WRL::ComPtr<ICoreWebView2_2> browser2;
    if (FAILED(browser.As(&browser2)) || !browser2) {
        return false;
    }
    auto state = std::make_shared<TransportState>();
    state->dispatcher = std::move(dispatcher);
    if (FAILED(browser2->get_Environment(state->environment.GetAddressOf())) ||
        !state->environment || !attach(window, state)) {
        return false;
    }
    active_state = std::move(state);
    return true;
}

void clear_transport(webview::webview &) {
    if (active_state) {
        active_state->dispatcher = Dispatcher{};
        active_state->events.clear();
        active_state->pending_event_bytes = 0;
        active_state.reset();
    }
}

bool shares_transport_context(webview::webview &first,
                              webview::webview &second) {
    return active_state && browser_of(first) && browser_of(second);
}

void authorize_view(webview::webview &window) {
    if (!active_state || !attach(window, active_state)) {
        throw std::runtime_error("Failed to authorize binary RPC WebView");
    }
}

bool post_event_bytes(webview::webview &window, Bytes &event) {
    if (!active_state || event.size() > max_message_size ||
        active_state->events.size() >= max_pending_events ||
        event.size() >
            max_pending_event_bytes - active_state->pending_event_bytes) {
        return false;
    }
    const auto token = active_state->next_event_id;
    if (token == 0 || active_state->events.contains(token)) {
        return false;
    }
    ++active_state->next_event_id;
    active_state->pending_event_bytes += event.size();
    active_state->events.emplace(token, std::move(event));
    try {
        window.eval("if(window.__APP_NATIVE_EVENT__)"
                    "window.__APP_NATIVE_EVENT__(" +
                    std::to_string(token) + ");");
    } catch (const std::exception &) {
        auto queued = active_state->events.find(token);
        if (queued != active_state->events.end()) {
            event = std::move(queued->second);
            active_state->pending_event_bytes -= event.size();
            active_state->events.erase(queued);
        }
        return false;
    }
    return true;
}

Bytes take_event_bytes(webview::webview &window, std::uint64_t token) {
    if (!active_state || !browser_of(window)) return {};
    auto found = active_state->events.find(token);
    if (found == active_state->events.end()) return {};
    Bytes body = std::move(found->second);
    active_state->pending_event_bytes -= body.size();
    active_state->events.erase(found);
    return body;
}

void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    if (!active_state) {
        window.set_html(html);
        return;
    }
    active_state->html = html;
    window.navigate(std::string(page_url));
}

} // namespace app::binary_rpc
