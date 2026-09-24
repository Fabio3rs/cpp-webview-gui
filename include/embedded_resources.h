#pragma once
// Stable declarations let the binding emitter run before the UI is bundled.

namespace embedded {

const char *index_html_str();

} // namespace embedded

#define INDEX_HTML (::embedded::index_html_str())
