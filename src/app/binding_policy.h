#pragma once

#include <string_view>

namespace app {

// A production URL supplied by the caller is untrusted application content.
[[nodiscard]] constexpr bool should_install_bindings(
    bool dev_mode, std::string_view custom_url) noexcept {
    return dev_mode || custom_url.empty();
}

} // namespace app
