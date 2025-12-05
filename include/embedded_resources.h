#pragma once
// =============================================================================
// embedded_resources.h - Interface para recursos embarcados
// =============================================================================
// Este header é gerado manualmente e provê a interface.
// Os dados são gerados automaticamente pelo CMake (EmbedFile.cmake).
// =============================================================================

#include <cstddef>
#include <string_view>

namespace embedded {

// Declarações - definições estão no .cpp gerado pelo CMake
extern const unsigned char index_html_data[];
[[maybe_unused]] extern const std::size_t index_html_size;
const char *index_html_str();

// Wrapper conveniente para C++17+
inline std::string_view index_html_view() {
    return {index_html_str(), index_html_size};
}

} // namespace embedded

// Alias para compatibilidade com código existente
// Antes: INDEX_HTML (string literal)
// Agora: INDEX_HTML (const char*)
#define INDEX_HTML (::embedded::index_html_str())
