#pragma once

#include <cstddef>
#include <functional>
#include <tuple>

namespace app::bindings {

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

} // namespace app::bindings
