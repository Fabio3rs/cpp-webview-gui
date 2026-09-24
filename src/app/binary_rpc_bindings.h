#pragma once

#include "app/binary_rpc.h"
#include "app/function_traits.h"
#include "app/wire_codec.h"

#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace app::binary_rpc {

// FNV-1a over UTF-8 binding names. Collision detection happens in Dispatcher.
[[nodiscard]] constexpr std::uint32_t method_id(std::string_view name) {
    constexpr std::uint32_t offset = 2166136261U;
    constexpr std::uint32_t prime = 16777619U;
    std::uint32_t hash = offset;
    for (const char character : name) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= prime;
    }
    return hash;
}

template <typename F, std::size_t... I>
constexpr bool has_borrowed_input_impl(std::index_sequence<I...>) {
    using Traits = bindings::function_traits<F>;
    return (false || ... ||
            std::is_same_v<std::decay_t<typename Traits::template arg<I>>,
                           BinaryView>);
}

template <typename F>
constexpr bool has_borrowed_input_v = has_borrowed_input_impl<F>(
    std::make_index_sequence<bindings::function_traits<F>::arity>{});

template <typename F, std::size_t... I>
void bind_wire_impl(Dispatcher &dispatcher, std::string_view name, F func,
                    std::index_sequence<I...>) {
    using Traits = bindings::function_traits<F>;
    using Result = typename Traits::result_type;
    static_assert(!std::is_same_v<std::decay_t<Result>, BinaryView>,
                  "BinaryView is an input-only wire type");
    dispatcher.bind(method_id(name), [callable = std::move(func)](
                                         Reader &reader, Writer &writer) {
        // Braced initialization decodes arguments in wire order.
        std::tuple<std::decay_t<typename Traits::template arg<I>>...> args{
            WireCodec<std::decay_t<typename Traits::template arg<I>>>::read(
                reader)...};
        reader.finish();
        if constexpr (std::is_void_v<Result>) {
            std::apply(callable, args);
        } else {
            auto result = std::apply(callable, args);
            WireCodec<std::decay_t<Result>>::write(writer, result);
        }
    });
}

// Register a statically typed handler without constructing a JSON DOM.
template <typename F>
void bind_wire(Dispatcher &dispatcher, std::string_view name, F func) {
    using Callable = std::decay_t<F>;
    using Traits = bindings::function_traits<Callable>;
    bind_wire_impl(dispatcher, name, Callable(std::move(func)),
                   std::make_index_sequence<Traits::arity>{});
}

} // namespace app::binary_rpc
