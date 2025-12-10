#include "app/application.h"
#include <atomic>

// Definição das variáveis estáticas atômicas e de sincronização
std::atomic<bool> app::Application::shutdown_requested_{false};
std::mutex app::Application::shutdown_mutex_;
std::condition_variable app::Application::shutdown_cv_;
