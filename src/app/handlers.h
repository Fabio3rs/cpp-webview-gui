#pragma once
// =============================================================================
// App Handlers - Handlers específicos desta aplicação
// =============================================================================

#include "app/bindings.h"
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
    bindings::bind_typed(w, "ping",
                         [&handlers](std::optional<std::string> msg) {
                             return handlers.ping(msg);
                         });
    bindings::bind_typed(w, "getVersion",
                         [&handlers]() { return handlers.get_version(); });
    bindings::bind_typed(w, "openFile", [&handlers](const std::string &path) {
        return handlers.open_file(path);
    });

    // Exemplo para handlers que preferem JSON bruto:
    // bindings::bind_json(w, "nomeNoJS", [](const json &args) { ... });
}

} // namespace app
