#include "dev_server.h"
#include "webview/webview.h"
#include <iostream>
#include <nlohmann/json.hpp>

// Em produção, inclui o HTML embutido
#ifndef APP_DEV_MODE
#include "index_html.h"
#endif

using json = nlohmann::json;

// Configuração do app
namespace config {
constexpr const char *APP_TITLE = "Prompt Workbench";
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
} // namespace config

// =============================================================================
// Setup de bindings JS <-> C++
// =============================================================================
void setup_bindings(webview::webview &w) {
    // Exemplo: ping/pong
    w.bind("ping", [&](const std::string &args_str) -> std::string {
        json args = json::parse(args_str);

        std::cout << "[APP] Ping from UI: " << args[0] << std::endl;

        json result = {{"code", 200}, {"message", "pong"}};
        return result.dump();
    });

    // Adicione mais bindings aqui...
}

// =============================================================================
// Main
// =============================================================================
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE /*hInst*/, HINSTANCE /*hPrevInst*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
#else
int main() {
#endif
    // Detecta modo dev
    bool dev_mode = dev::is_dev_mode();
    dev::ServerProcess dev_server_proc;
    std::string dev_url;

    std::cout << "[APP] Modo: " << (dev_mode ? "DEVELOPMENT" : "PRODUCTION")
              << std::endl;

    // Em dev, inicia o Vite dev server
    if (dev_mode) {
        dev::ServerConfig cfg = dev::get_default_config();
        dev_url = cfg.dev_url;

        if (!dev::ensure_server_running(cfg, dev_server_proc)) {
            std::cerr << "[APP] Falha ao iniciar dev server. Abortando."
                      << std::endl;
            return 1;
        }
    }

    try {
        // DevTools: habilitado em dev, desabilitado em prod
        webview::webview main_window(dev_mode, nullptr);
        main_window.set_title(config::APP_TITLE);
        main_window.set_size(config::WINDOW_WIDTH, config::WINDOW_HEIGHT,
                             WEBVIEW_HINT_NONE);

        // Setup bindings
        setup_bindings(main_window);

        // Carrega conteúdo
        if (dev_mode) {
            std::cout << "[APP] Navegando para " << dev_url << std::endl;
            main_window.navigate(dev_url);
        } else {
#ifdef APP_DEV_MODE
            // Fallback se compilado sem o header
            std::cerr << "[APP] ERRO: Build de dev sem Vite server!"
                      << std::endl;
            return 1;
#else
            std::cout << "[APP] Carregando HTML embutido..." << std::endl;
            main_window.set_html(INDEX_HTML);
#endif
        }

        std::cout << "[APP] Iniciando event loop..." << std::endl;

        // Run event loop
        main_window.run();

    } catch (const webview::exception &e) {
        std::cerr << "[APP] Erro WebView: " << e.what() << std::endl;

        // Cleanup em caso de erro
        if (dev_mode) {
            dev::stop_server(dev_server_proc);
        }
        return 1;
    }

    // Cleanup: encerra dev server se fomos nós que iniciamos
    if (dev_mode) {
        dev::stop_server(dev_server_proc);
    }

    std::cout << "[APP] Encerrado." << std::endl;
    return 0;
}
