#pragma once
// =============================================================================
// App Handlers - Handlers específicos desta aplicação
// =============================================================================

#include "app/bindings_with_meta.h"
#include "app/config.h"

namespace app {

// =============================================================================
// HandlerRegistry - Handlers da aplicação com injeção de dependências
// =============================================================================
class HandlerRegistry {
  public:
    // Interface para logging (dependency injection)
    struct Logger {
        virtual ~Logger() = default;
        virtual void log(const std::string &msg) = 0;
    };

    // Logger padrão que usa std::cout
    struct DefaultLogger : Logger {
        void log(const std::string &msg) override {
            std::cout << msg << std::endl;
        }
    };

    explicit HandlerRegistry(
        std::unique_ptr<Logger> logger = std::make_unique<DefaultLogger>())
        : logger_(std::move(logger)) {}

    [[nodiscard]] nlohmann::json
    ping(std::optional<std::string> message) const {
        const std::string ping_message = message.value_or("");
        if (logger_)
            logger_->log("[APP] Ping from UI: " + ping_message);
        return {{"message", "pong"}, {"echo", ping_message}};
    }

    [[nodiscard]] nlohmann::json get_version() const {
        return {{"version", config::VERSION}};
    }

    [[nodiscard]] nlohmann::json open_file(const std::string &path) const {
        if (path.empty()) {
            throw bindings::BindingError("Path not provided",
                                         bindings::ErrorCode::MissingArg);
        }
        if (logger_)
            logger_->log("[APP] Opening file: " + path);
        // TODO: Implement file opening logic
        return {{"path", path}, {"status", "opened"}};
    }

  private:
    std::unique_ptr<Logger> logger_;
};

// =============================================================================
// setup - Registro dos bindings da aplicação
// =============================================================================

inline void setup(webview::webview &w, const HandlerRegistry &handlers) {
    // Handlers que retornam JSON estruturado - mantêm bind_typed
    APP_BIND_TYPED(w, "ping", [&handlers](std::optional<std::string> msg) {
        return handlers.ping(msg);
    });
    APP_BIND_TYPED(w, "getVersion",
                   [&handlers]() { return handlers.get_version(); });
    APP_BIND_TYPED(w, "openFile", [&handlers](const std::string &path) {
        return handlers.open_file(path);
    });

    // =============================================================================
    // Exemplos de bind_generic - handlers que retornam qualquer tipo
    // conversível para JSON
    // =============================================================================

    // Exemplo: migrando getVersion para bind_generic (mantém compatibilidade
    // JSON) bindings::bind_generic(w, "getVersion", [&handlers]() {
    //     return handlers.get_version();  // Retorna json diretamente
    // });

    // Tipos simples - usando bind_generic para flexibilidade
    APP_BIND_TYPED(w, "getCounter", []() { return 42; });
    APP_BIND_TYPED(w, "getPi", []() { return 3.14159; });
    APP_BIND_TYPED(w, "getStatus", []() { return std::string("online"); });
    APP_BIND_TYPED(w, "isReady", []() { return true; });

    // JSON - retorna diretamente (sem embrulho)
    APP_BIND_TYPED(w, "getConfig", ([]() {
                       return nlohmann::json{{"theme", "dark"},
                                             {"lang", "pt-br"}};
                   }));

    // Para tipos customizados, especialize to_json_value:
    // namespace app::bindings {
    //   template<>
    //   inline json to_json_value(const MeuTipo &v) { return json{{"field",
    //   v.field}}; }
    // }

    // Exemplo de tipo customizado com especialização
    struct AppInfo {
        std::string name;
        int version;
        bool debug;
    };

    // Especialização para AppInfo
    // namespace app::bindings {
    //   template<>
    //   inline json to_json_value(const AppInfo &info) {
    //       return json{
    //           {"name", info.name},
    //           {"version", info.version},
    //           {"debug", info.debug}
    //       };
    //   }
    // }

    // Handler que retorna AppInfo (comentado para não conflitar)
    // bindings::bind_generic(w, "getAppInfo", []() {
    //     return AppInfo{"My App", 1, true};
    // });
}

} // namespace app
