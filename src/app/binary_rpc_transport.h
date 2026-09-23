#pragma once

#include "app/binary_rpc.h"
#include "webview/webview.h"

namespace app::binary_rpc {

// Registers the in-process byte transport before the first page loads.
[[nodiscard]] bool install_transport(webview::webview &window,
                                     Dispatcher dispatcher);
// Drop callbacks captured by the dispatcher when its application shuts down.
void clear_transport(webview::webview &window);
// A registered scheme is shared only by views in the same WebKit context.
[[nodiscard]] bool shares_transport_context(webview::webview &first,
                                            webview::webview &second);
// Queues bytes for one application page and signals it with a small JS token.
[[nodiscard]] bool post_event_bytes(webview::webview &window, Bytes &event);
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html);

} // namespace app::binary_rpc
