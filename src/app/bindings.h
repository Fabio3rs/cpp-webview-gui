#pragma once
// =============================================================================
// Bindings - Handlers para comunicação JS <-> C++
// =============================================================================

#include "app/config.h"
#include "webview/webview.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace app::bindings {

using json = nlohmann::json;

// =============================================================================
// Helpers para criar respostas padronizadas
// =============================================================================

inline json ok(const json &data = nullptr) {
    json response = {{"ok", true}};
    if (!data.is_null()) {
        response["data"] = data;
    }
    return response;
}

inline json error(const std::string &message, int code = 500) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}

// =============================================================================
// Handlers individuais - Adicione novos handlers aqui
// =============================================================================

namespace handlers {

// Ping/Pong - teste de comunicação
inline std::string ping(const std::string &args_str) {
    json args = json::parse(args_str);

    // args[0] é o primeiro argumento passado do JS
    std::string message = args.empty() ? "" : args[0].dump();
    std::cout << "[APP] Ping from UI: " << message << std::endl;

    return ok({{"message", "pong"}}).dump();
}

// Obter versão do app
inline std::string get_version([[maybe_unused]] const std::string &args_str) {
    return ok({{"version", config::VERSION}}).dump();
}

// Exemplo: abrir arquivo (para futuras features)
inline std::string open_file(const std::string &args_str) {
    json args = json::parse(args_str);

    if (args.empty() || !args[0].is_string()) {
        return error("Path não fornecido", 400).dump();
    }

    std::string path = args[0].get<std::string>();
    std::cout << "[APP] Abrindo arquivo: " << path << std::endl;

    // TODO: implementar lógica de abrir arquivo
    return ok({{"path", path}, {"status", "opened"}}).dump();
}

} // namespace handlers

// =============================================================================
// Registro de todos os bindings
// =============================================================================

inline void setup(webview::webview &w) {
    // Registra cada handler com seu nome JS
    w.bind("ping", handlers::ping);
    w.bind("getVersion", handlers::get_version);
    w.bind("openFile", handlers::open_file);

    // Adicione novos bindings aqui:
    // w.bind("nomeNoJS", handlers::nome_handler);
}

} // namespace app::bindings
