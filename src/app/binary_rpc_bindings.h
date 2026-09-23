#pragma once

#include "app/binary_rpc.h"
#include "app/bindings.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace app::binary_rpc {

// FNV-1a over UTF-8 binding names. Collision detection happens in Dispatcher.
[[nodiscard]] inline std::uint32_t method_id(std::string_view name) {
    constexpr std::uint32_t offset = 2166136261U;
    constexpr std::uint32_t prime = 16777619U;
    std::uint32_t hash = offset;
    for (const char character : name) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= prime;
    }
    return hash;
}

template <typename F>
void bind_cbor(Dispatcher &dispatcher, std::string_view name, F func) {
    using Callable = std::decay_t<F>;
    using Result = typename bindings::function_traits<Callable>::result_type;
    dispatcher.bind(method_id(name), [callable = Callable(std::move(func))](
                                         Reader &reader, Writer &writer) {
        bindings::json response;
        try {
            const auto input = reader.remaining();
            const auto args =
                bindings::json::from_cbor(input.begin(), input.end());
            if (!args.is_array()) {
                throw bindings::BindingError("Arguments must be an array",
                                             bindings::ErrorCode::InvalidArgs);
            }
            if constexpr (std::is_void_v<Result>) {
                bindings::call_with_json_args(callable, args);
                response = bindings::ok(bindings::json::object());
            } else {
                auto value = bindings::call_with_json_args(callable, args);
                response = bindings::ok(
                    bindings::JsConv<std::decay_t<Result>>::to_json(value));
            }
        } catch (const bindings::BindingError &error) {
            response = bindings::error(error.what(), error.code());
        } catch (const bindings::json::exception &error) {
            response =
                bindings::error(error.what(), bindings::ErrorCode::InvalidJson);
        } catch (const std::exception &error) {
            response = bindings::error(error.what(),
                                       bindings::ErrorCode::InternalError);
        }
        const auto output = bindings::json::to_cbor(response);
        writer.raw(output);
    });
}

} // namespace app::binary_rpc
