#pragma once
// =============================================================================
// Bindings - Handlers para comunicação JS <-> C++
// =============================================================================

#include "webview/webview.h"
#include <cassert> // Para asserts (NASA-style)
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace app::bindings {

using json = nlohmann::json;

// =============================================================================
// Códigos de erro padronizados (type safety)
// =============================================================================
enum class ErrorCode {
    InvalidJson = 400,
    InvalidArgs = 400,
    MissingArg = 400,
    TypeMismatch = 400,
    InternalError = 500
};

// =============================================================================
// Helpers para criar respostas padronizadas (imutáveis, RAII)
// =============================================================================

[[nodiscard]] inline json ok(const json &data = nullptr) {
    json response = {{"ok", true}};
    if (!data.is_null()) {
        response["data"] = data;
    }
    return response;
}

[[nodiscard]] inline json error(const std::string &message, ErrorCode code) {
    return {
        {"ok", false},
        {"error", {{"code", static_cast<int>(code)}, {"message", message}}}};
}

// =============================================================================
// BindingError - erro customizado com código (RAII, strong type)
// =============================================================================

class BindingError : public std::runtime_error {
  public:
    explicit BindingError(std::string message, ErrorCode code)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

  private:
    ErrorCode code_;
};

// =============================================================================
// Camada 0 - Bind "cru" (string -> string)
// =============================================================================

using RawHandler = std::function<std::string(const std::string &)>;

inline void bind_raw(webview::webview &w, std::string name,
                     RawHandler handler) {
    w.bind(std::move(name),
           [handler = std::move(handler)](const std::string &args_str) {
               return handler(args_str);
           });
}

// =============================================================================
// Camada 1 - Handlers que recebem/retornam json
// =============================================================================

using JsonHandler = std::function<json(const json &)>;

inline json parse_args(const std::string &args_str) {
    if (args_str.empty()) {
        return json::array();
    }
    return json::parse(args_str);
}

inline void bind_json(webview::webview &w, std::string name,
                      JsonHandler handler) {
    bind_raw(
        w, std::move(name),
        [handler =
             std::move(handler)](const std::string &args_str) -> std::string {
            json args;
            try {
                args = parse_args(args_str);
            } catch (const std::exception &e) {
                return error(std::string("JSON inválido: ") + e.what(),
                             ErrorCode::InvalidJson)
                    .dump();
            }

            try {
                if (!args.is_array()) {
                    throw BindingError("Argumentos devem ser um array JSON",
                                       ErrorCode::InvalidArgs);
                }

                json result = handler(args);
                return ok(result).dump();
            } catch (const BindingError &e) {
                return error(e.what(), e.code()).dump();
            } catch (const json::type_error &e) {
                return error(std::string("Argumento inválido: ") + e.what(),
                             ErrorCode::TypeMismatch)
                    .dump();
            } catch (const json::out_of_range &e) {
                return error(std::string("Argumento fora do intervalo: ") +
                                 e.what(),
                             ErrorCode::MissingArg)
                    .dump();
            } catch (const std::exception &e) {
                return error(std::string("Erro interno: ") + e.what(),
                             ErrorCode::InternalError)
                    .dump();
            }
        });
}

// =============================================================================
// JsConv - Conversões seguras com validações (type safety aprimorada)
// =============================================================================
template <typename T, typename Enable = void> struct JsConv {
    [[nodiscard]] static T from_json(const json &j) {
        if constexpr (std::is_arithmetic_v<T>) {
            if (!j.is_number()) {
                throw BindingError("Expected number for type " +
                                       std::string(typeid(T).name()),
                                   ErrorCode::TypeMismatch);
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!j.is_string()) {
                throw BindingError("Expected string", ErrorCode::TypeMismatch);
            }
        }
        // Adicione validações para outros tipos conforme necessário
        return j.get<T>();
    }

    [[nodiscard]] static json to_json(const T &value) { return json(value); }
};

template <> struct JsConv<json> {
    static const json &from_json(const json &j) { return j; }
    static json to_json(const json &value) { return value; }
};

template <typename T> struct JsConv<std::optional<T>> {
    static std::optional<T> from_json(const json &j) {
        if (j.is_null()) {
            return std::nullopt;
        }
        return JsConv<T>::from_json(j);
    }

    static json to_json(const std::optional<T> &value) {
        if (!value) {
            return nullptr;
        }
        return JsConv<T>::to_json(*value);
    }
};

template <typename T> struct function_traits;

template <typename R, typename... Args> struct function_traits<R(Args...)> {
    using result_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr std::size_t arity = sizeof...(Args);

    template <std::size_t I> using arg = std::tuple_element_t<I, args_tuple>;
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename... Args>
struct function_traits<R (&)(Args...)> : function_traits<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {
};

template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>>
    : function_traits<R(Args...)> {};

template <typename F>
struct function_traits : function_traits<decltype(&F::operator())> {};

inline const json &arg_or_null(const json &args, std::size_t index) {
    static const json null_json = nullptr;
    if (index < args.size()) {
        return args.at(index);
    }
    return null_json;
}

template <typename Callable, typename Tuple, std::size_t... I>
decltype(auto) call_with_json_args_impl(Callable &&callable, const json &args,
                                        std::index_sequence<I...>) {
    return std::invoke(
        std::forward<Callable>(callable),
        JsConv<std::decay_t<std::tuple_element_t<I, Tuple>>>::from_json(
            arg_or_null(args, I))...);
}

template <typename Callable>
decltype(auto) call_with_json_args(Callable &&callable, const json &args) {
    using traits = function_traits<std::decay_t<Callable>>;
    using tuple_type = typename traits::args_tuple;
    return call_with_json_args_impl<Callable, tuple_type>(
        std::forward<Callable>(callable), args,
        std::make_index_sequence<traits::arity>{});
}

template <typename F>
void bind_typed(webview::webview &w, std::string name, F &&func) {
    using Callable = std::decay_t<F>;
    using traits = function_traits<Callable>;
    using result_t = typename traits::result_type;

    // Assert para invariants (NASA-style)
    static_assert(traits::arity <= 10,
                  "Too many arguments for binding"); // Bounded arity

    bind_json(
        w, std::move(name),
        [callable = Callable(std::forward<F>(func))](const json &args) -> json {
            // Validação de argumentos (error checking)
            if (!args.is_array()) {
                throw BindingError("Arguments must be a JSON array",
                                   ErrorCode::InvalidArgs);
            }
            if (args.size() > traits::arity) {
                // Permitir extras, mas logar (não fatal)
                std::cout << "[WARNING] Extra arguments ignored\n";
            }

            try {
                if constexpr (std::is_void_v<result_t>) {
                    call_with_json_args(callable, args);
                    return json::object();
                } else {
                    auto result = call_with_json_args(callable, args);
                    return JsConv<std::decay_t<result_t>>::to_json(result);
                }
            } catch (const BindingError &) {
                throw; // Re-throw custom errors
            } catch (const std::exception &e) {
                throw BindingError(std::string("Internal error: ") + e.what(),
                                   ErrorCode::InternalError);
            }
        });
}

} // namespace app::bindings
