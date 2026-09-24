#pragma once

#include "app/binary_rpc.h"
#include "webview/webview.h"

#include <memory>
#include <string_view>

namespace app::binary_rpc {

#if defined(_WIN32)
inline constexpr std::string_view rpc_base =
    "https://cpp-webview-gui.invalid/rpc/";
inline constexpr std::string_view page_url =
    "https://cpp-webview-gui.invalid/index.html";
#else
inline constexpr std::string_view rpc_base = "app-rpc://native/";
inline constexpr std::string_view page_url = "app-rpc://native/index.html";
#endif

// Creates a view with the macOS scheme configured before WKWebView exists.
#if defined(__APPLE__)
[[nodiscard]] std::unique_ptr<webview::webview> create_webview(bool debug,
                                                               void *parent);
#else
[[nodiscard]] inline std::unique_ptr<webview::webview>
create_webview(bool debug, void *parent) {
    return std::make_unique<webview::webview>(debug, parent);
}
#endif

// Registers the in-process byte transport before the first page loads.
[[nodiscard]] bool install_transport(webview::webview &window,
                                     Dispatcher dispatcher);
// Drop callbacks captured by the dispatcher when its application shuts down.
void clear_transport(webview::webview &window);
// A registered scheme is shared only by views in the same WebKit context.
[[nodiscard]] bool shares_transport_context(webview::webview &first,
                                            webview::webview &second);
// Grants a guarded application WebView access to the context-wide scheme.
void authorize_view(webview::webview &window);
// Queues bytes for one application page and signals it with a small JS token.
[[nodiscard]] bool post_event_bytes(webview::webview &window, Bytes &event);
// Consumes a queued event for the development loopback endpoint.
[[nodiscard]] Bytes take_event_bytes(webview::webview &window,
                                     std::uint64_t token);
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html);

} // namespace app::binary_rpc
