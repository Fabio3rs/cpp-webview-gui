#pragma once

#include "app/binary_rpc.h"
#include "webview/webview.h"

namespace app::binary_rpc {

// Registers the in-process byte transport before the first page loads.
[[nodiscard]] bool install_transport(webview::webview &window,
                                     Dispatcher dispatcher);
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html);

} // namespace app::binary_rpc
