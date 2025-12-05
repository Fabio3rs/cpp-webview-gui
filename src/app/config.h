#pragma once
// =============================================================================
// App Configuration - Configurações centralizadas da aplicação
// =============================================================================

namespace app::config {

// Informações da janela
constexpr const char *WINDOW_TITLE = "Prompt Workbench";
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;

// Versão (pode ser injetada pelo CMake)
#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

constexpr const char *VERSION = APP_VERSION;

} // namespace app::config
