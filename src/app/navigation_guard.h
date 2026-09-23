#pragma once

#include "webview/webview.h"

#include <string_view>

namespace app {

// Blocks foreign navigation before it can replace a privileged document.
// Returns false on backends that do not yet implement this guard.
[[nodiscard]] bool install_navigation_guard(webview::webview &window,
                                            std::string_view trusted_url);

} // namespace app
