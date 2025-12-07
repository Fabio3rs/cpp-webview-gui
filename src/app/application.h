#pragma once
// =============================================================================
// Application - Classe principal que encapsula a lógica do app
// =============================================================================

#include "app/cli_options.h"
#include "app/config.h"
#include "app/handlers.h"
#include "dev_server.h"
#include "webview/webview.h"
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "app/shutdown_monitor.h"
#include <string>

// Em produção, inclui o header com o HTML embutido
#ifndef APP_DEV_MODE
#include "embedded_resources.h"
#endif

namespace app {

class Application {
  public:
    // Construtor padrão - auto-detecta modo
    Application() : Application(Options{}) {}

    // Construtor com opções da CLI
    explicit Application(const Options &opts)
        : options_(opts), dev_mode_(resolve_dev_mode(opts)),
          verbose_(opts.verbose) {}

    ~Application() { cleanup(); }

    // Non-copyable, non-movable
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    Application(Application &&) = delete;
    Application &operator=(Application &&) = delete;

    // =========================================================================
    // Inicialização
    // =========================================================================
    bool initialize() {
        log_mode();

        // Configura signal handlers para graceful shutdown
        setup_signal_handlers();

        if (dev_mode_ && !start_dev_server()) {
            return false;
        }

        return create_window();
    }

    // =========================================================================
    // Execução principal
    // =========================================================================
    int run() {
        if (!window_) {
            std::cerr << "[APP] Erro: janela não inicializada" << std::endl;
            return 1;
        }

        try {
            load_content();
            std::cout << "[APP] Iniciando event loop..." << std::endl;

            // Inicia ShutdownMonitor (classe separada)
            ShutdownMonitor shutdown_monitor(
                [this]() { return should_shutdown(); },
                [this]() {
                    if (window_) {
                        window_->terminate();
                    }
                });

            // Executa o loop principal da webview
            window_->run();
        } catch (const webview::exception &e) {
            std::cerr << "[APP] Erro WebView: " << e.what() << std::endl;
            shutdown_cv_.notify_all();
            return 1;
        } catch (const std::exception &e) {
            std::cerr << "[APP] Erro inesperado: " << e.what() << std::endl;
            shutdown_cv_.notify_all();
        }

        return 0;
    }

  private:
    // =========================================================================
    // Métodos privados
    // =========================================================================

    void log_mode() const {
        std::cout << "[APP] Modo: "
                  << (dev_mode_ ? "DEVELOPMENT" : "PRODUCTION") << std::endl;
    }

    bool start_dev_server() {
        dev::ServerConfig cfg = dev::get_default_config();
        dev_url_ = cfg.dev_url;

        if (!dev::ensure_server_running(cfg, dev_server_)) {
            std::cerr << "[APP] Falha ao iniciar dev server. Abortando."
                      << std::endl;
            return false;
        }

        return true;
    }

    bool create_window() {
        try {
            // DevTools habilitado apenas em dev
            window_ = std::make_unique<webview::webview>(dev_mode_, nullptr);
            window_->set_title(config::WINDOW_TITLE);

            // Usa tamanho das opções CLI ou padrão do config
            int width =
                options_.width > 0 ? options_.width : config::WINDOW_WIDTH;
            int height =
                options_.height > 0 ? options_.height : config::WINDOW_HEIGHT;
            window_->set_size(width, height, WEBVIEW_HINT_NONE);

            // Setup bindings
            app::setup(*window_, handlers_);

            return true;
        } catch (const webview::exception &e) {
            std::cerr << "[APP] Erro ao criar janela: " << e.what()
                      << std::endl;
            return false;
        }
    }

    void load_content() {
        // URL customizada tem prioridade
        if (!options_.url.empty()) {
            std::cout << "[APP] Navegando para URL customizada: "
                      << options_.url << std::endl;
            window_->navigate(options_.url);
            return;
        }

        if (dev_mode_) {
            std::cout << "[APP] Navegando para " << dev_url_ << std::endl;
            window_->navigate(dev_url_);
        } else {
#ifdef APP_DEV_MODE
            throw std::runtime_error("Build de dev sem Vite server!");
#else
            std::cout << "[APP] Carregando HTML embutido..." << std::endl;
            window_->set_html(INDEX_HTML);
#endif
        }
    }

    void cleanup() {
        if (dev_mode_ && dev_server_.owned) {
            dev::stop_server(dev_server_);
        }
        log("[APP] Encerrado.");
    }

    void log(std::string_view msg) const { std::cout << msg << std::endl; }

    void log_verbose(std::string_view msg) const {
        if (verbose_) {
            std::cout << msg << std::endl;
        }
    }

    // Resolve modo dev baseado nas opções e compile-time flags
    static bool resolve_dev_mode(const Options &opts) {
        // Opções explícitas têm prioridade
        if (opts.prod_mode)
            return false;
        if (opts.dev_mode)
            return true;
        // Fallback para detecção automática
        return dev::is_dev_mode();
    }

    // =========================================================================
    // Signal handling para graceful shutdown
    // =========================================================================
    static std::atomic<bool> shutdown_requested_;
    static std::mutex shutdown_mutex_;
    static std::condition_variable shutdown_cv_;

    static void signal_handler(int signal) {
        // Evita múltiplas impressões do mesmo sinal
        static std::atomic<bool> signal_printed{false};
        if (!signal_printed.exchange(true)) {
            std::cout << "\n[APP] Sinal " << signal
                      << " recebido, iniciando shutdown graceful..."
                      << std::endl;
        }
        shutdown_requested_.store(true);
        shutdown_cv_.notify_all(); // Notifica todas as threads esperando
    }

    void setup_signal_handlers() {
        // Configura handlers para sinais comuns
        std::signal(SIGINT, signal_handler);  // Ctrl+C
        std::signal(SIGTERM, signal_handler); // kill ou systemd

        // No Windows, SIGBREAK também pode ser útil
#ifdef _WIN32
        std::signal(SIGBREAK, signal_handler);
#endif

        shutdown_requested_.store(false);
        std::cout << "[APP] Signal handlers configurados para graceful shutdown"
                  << std::endl;
    }

    bool should_shutdown() const { return shutdown_requested_.load(); }

    // =========================================================================
    // Membros
    // =========================================================================
    Options options_;
    bool dev_mode_;
    bool verbose_ = false;
    std::string dev_url_;
    dev::ServerProcess dev_server_;
    app::HandlerRegistry handlers_;
    std::unique_ptr<webview::webview> window_;
};

} // namespace app
