#pragma once
// =============================================================================
// App Handlers - Handlers específicos desta aplicação
// =============================================================================

#include "app/bindings_with_meta.h"
#include "app/config.h"
#include "app/handler_types.h"
#include "app/native_types.h"
#include <cstdint>
#include <limits>

namespace app {

// =============================================================================
// HandlerRegistry - Handlers da aplicação com injeção de dependências
// =============================================================================
class HandlerRegistry {
  public:
    // Interface para logging (dependency injection)
    struct Logger {
        virtual ~Logger() = default;
        Logger() = default;
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger(Logger &&) = delete;
        Logger &operator=(Logger &&) = delete;
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

    [[nodiscard]] PingResult ping(std::optional<std::string> message) const {
        const std::string ping_message = message.value_or("");
        if (logger_)
            logger_->log("[APP] Ping from UI: " + ping_message);
        return {"pong", ping_message};
    }

    [[nodiscard]] VersionInfo get_version() const { return {config::VERSION}; }

    [[nodiscard]] FileOpenInfo open_file(const std::string &path) const {
        if (path.empty()) {
            throw bindings::BindingError("Path not provided",
                                         bindings::ErrorCode::MissingArg);
        }
        if (logger_)
            logger_->log("[APP] Opening file: " + path);
        // TODO: Implement file opening logic
        return {path, "opened"};
    }

  private:
    std::unique_ptr<Logger> logger_;
};

// =============================================================================
// setup - Registro dos bindings da aplicação
// =============================================================================

inline void setup(webview::webview *w, const HandlerRegistry &handlers,
                  binary_rpc::Dispatcher *binary = nullptr) {
    // Each handler has a direct wire codec when binary transport is available.
    APP_BIND_TYPED_WIRE(w, binary, "ping",
                        [&handlers](std::optional<std::string> msg) {
                            return handlers.ping(msg);
                        });
    APP_BIND_TYPED_WIRE(w, binary, "getVersion",
                        [&handlers]() { return handlers.get_version(); });
    APP_BIND_TYPED_WIRE(w, binary, "openFile",
                        [&handlers](const std::string &path) {
                            return handlers.open_file(path);
                        });

    APP_BIND_TYPED_WIRE(w, binary, "getCounter", []() { return 42; });
    APP_BIND_TYPED_WIRE(w, binary, "getPi", []() {
        constexpr double pi_example = 3.14159;
        return pi_example;
    });
    APP_BIND_TYPED_WIRE(w, binary, "getStatus",
                        []() { return std::string("online"); });
    APP_BIND_TYPED_WIRE(w, binary, "isReady", []() { return true; });

    APP_BIND_TYPED_WIRE(w, binary, "getConfig",
                        ([]() { return ConfigInfo{"dark", "pt-br"}; }));

    // Example bulk payload and checked arithmetic use the same generated
    // contract as every other application binding.
    APP_BIND_TYPED_WIRE(w, binary, "echoBytes",
                        [](const binary_rpc::Bytes &bytes) { return bytes; });
    APP_BIND_TYPED_WIRE(
        w, binary, "addI32", [](std::int32_t left, std::int32_t right) {
            const auto sum = static_cast<std::int64_t>(left) +
                             static_cast<std::int64_t>(right);
            if (sum < (std::numeric_limits<std::int32_t>::min)() ||
                sum > (std::numeric_limits<std::int32_t>::max)()) {
                throw binary_rpc::WireError("Integer overflow");
            }
            return static_cast<std::int32_t>(sum);
        });
}

inline void setup(webview::webview &w, const HandlerRegistry &handlers,
                  binary_rpc::Dispatcher *binary = nullptr) {
    setup(&w, handlers, binary);
}

} // namespace app
