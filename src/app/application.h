#pragma once
// =============================================================================
// Application - Classe principal que encapsula a lógica do app
// =============================================================================

#include "app/cli_options.h"
#include "app/config.h"
#include "app/handlers.h"
#include "app/shutdown_monitor.h"
#include "app/window_manager.h"
#include "dev_server.h"
#include "webview/webview.h"
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

// Em produção, inclui o header com o HTML embutido
#if !defined(APP_DEV_MODE) && !defined(APP_NO_EMBEDDED_UI)
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
            window_->init("window.__APP_WINDOW_ID__ = \"main\";");

            // Setup window manager and bindings
            window_manager_ = std::make_unique<WindowManager>(
                *window_, dev_mode_, dev_url_, options_.url, width, height,
                config::WINDOW_TITLE);
            window_manager_->set_bindings_setup(
                [this](webview::webview &w) { setup_bindings(w); });
            setup_bindings(*window_);

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
#if defined(APP_DEV_MODE)
            throw std::runtime_error("Build de dev sem Vite server!");
#elif defined(APP_NO_EMBEDDED_UI)
            std::cout << "[APP] UI embutida indisponível, usando HTML vazio."
                      << std::endl;
            window_->set_html("<!doctype html><html><body></body></html>");
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

    void setup_bindings(webview::webview &w) {
        app::setup(w, handlers_);
        if (!window_manager_) {
            return;
        }

        APP_BIND_TYPED(w, "createNativeWindow",
                       [this](app::bindings::json bootstrap) {
                           return window_manager_->create_window(bootstrap);
                       });
        APP_BIND_TYPED(w, "getBootstrap", [this](const std::string &window_id) {
            auto bootstrap = window_manager_->take_bootstrap(window_id);
            if (!bootstrap) {
                throw app::bindings::BindingError(
                    "Bootstrap not found",
                    app::bindings::ErrorCode::MissingArg);
            }
            return *bootstrap;
        });
        APP_BIND_TYPED(
            w, "postNativeEvent",
            [this](const std::string &window_id, app::bindings::json event) {
                if (!window_manager_->post_event(window_id, event)) {
                    throw app::bindings::BindingError(
                        "Window not found",
                        app::bindings::ErrorCode::MissingArg);
                }
            });
        APP_BIND_TYPED(w, "closeNativeWindow",
                       [this](const std::string &window_id) {
                           if (!window_manager_->close_window(window_id)) {
                               throw app::bindings::BindingError(
                                   "Window not found",
                                   app::bindings::ErrorCode::MissingArg);
                           }
                       });
        APP_BIND_TYPED(w, "listNativeWindows",
                       [this]() { return window_manager_->list_windows(); });
        APP_BIND_TYPED(
            w, "startNativeDrag",
            [this](const std::string &window_id, app::bindings::json payload) {
                window_manager_->start_drag_tracking(window_id, payload);
            });
        APP_BIND_TYPED(w, "completeNativeDrag",
                       [this](const std::string &target_window_id) {
                           return window_manager_->complete_drag_tracking(
                               target_window_id);
                       });
        APP_BIND_TYPED(w, "stopNativeDrag",
                       [this]() { window_manager_->stop_drag_tracking(); });
        APP_BIND_TYPED(w, "completeNativeDragOutside",
                       [this](const std::string &window_id) {
                           return window_manager_->complete_drag_outside(
                               window_id);
                       });
    }

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
    std::unique_ptr<WindowManager> window_manager_;
};

} // namespace app
