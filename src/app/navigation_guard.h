#pragma once

#include "webview/webview.h"

#include <string_view>

namespace app {

// Blocks foreign navigation before it can replace a privileged document.
// An empty URL identifies an embedded document on platforms with opaque origins.
[[nodiscard]] bool install_navigation_guard(webview::webview &window,
                                            std::string_view trusted_url);

} // namespace app
