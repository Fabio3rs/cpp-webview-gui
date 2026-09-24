#pragma once

#include "app/function_traits.h"
#include "app/wire_codec.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace app::binary_rpc {

// Produces JavaScript source at build time. Unsupported types fail to compile.
template <typename T> struct JsWire;

template <> struct JsWire<void> {
    static std::string read(std::string_view) { return "({})"; }
};

enum class PrimitiveWire { i32, u32, f64, string, bytes };

template <PrimitiveWire Operation> struct JsWirePrimitive {
    static constexpr std::string_view operation_name() {
        if constexpr (Operation == PrimitiveWire::i32)
            return "i32";
        if constexpr (Operation == PrimitiveWire::u32)
            return "u32";
        if constexpr (Operation == PrimitiveWire::f64)
            return "f64";
        if constexpr (Operation == PrimitiveWire::string)
            return "string";
        return "bytes";
    }
    static std::string write(std::string_view writer, std::string_view value) {
        return std::string(writer) + "." + std::string(operation_name()) + "(" +
               std::string(value) + ");";
    }
    static std::string read(std::string_view reader) {
        return std::string(reader) + "." + std::string(operation_name()) + "()";
    }
};

template <>
struct JsWire<std::int32_t> : JsWirePrimitive<PrimitiveWire::i32> {};
template <>
struct JsWire<std::uint32_t> : JsWirePrimitive<PrimitiveWire::u32> {};
template <> struct JsWire<double> : JsWirePrimitive<PrimitiveWire::f64> {};
template <>
struct JsWire<std::string> : JsWirePrimitive<PrimitiveWire::string> {};
template <> struct JsWire<Bytes> : JsWirePrimitive<PrimitiveWire::bytes> {};

template <> struct JsWire<bool> {
    static std::string write(std::string_view writer, std::string_view value) {
        return std::string(writer) + ".u8(" + std::string(value) + " ? 1 : 0);";
    }
    static std::string read(std::string_view reader) {
        return "readWireBool(" + std::string(reader) + ")";
    }
};

template <typename T> struct JsWire<std::optional<T>> {
    static std::string write(std::string_view writer, std::string_view value) {
        return std::string(writer) + ".optional(" + std::string(value) +
               ", (output, item) => {" + JsWire<T>::write("output", "item") +
               "});";
    }
    static std::string read(std::string_view reader) {
        return std::string(reader) + ".optional(input => " +
               JsWire<T>::read("input") + ")";
    }
};

template <typename T> struct JsWire<std::vector<T>> {
    static std::string write(std::string_view writer, std::string_view value) {
        return std::string(writer) + ".vector(" + std::string(value) +
               ", (output, item) => {" + JsWire<T>::write("output", "item") +
               "});";
    }
    static std::string read(std::string_view reader) {
        return std::string(reader) + ".vector(input => " +
               JsWire<T>::read("input") + ")";
    }
};

template <HasWireFields T> struct JsWire<T> {
    static std::string write(std::string_view writer, std::string_view value) {
        std::string result;
        std::apply(
            [&](auto... fields) {
                ((result +=
                  JsWire<typename decltype(fields)::value_type>::write(
                      writer, std::string(value) + "." + fields.name)),
                 ...);
            },
            WireFields<T>::fields());
        return result;
    }
    static std::string read(std::string_view reader) {
        std::string result = "({";
        std::apply(
            [&](auto... fields) {
                ((result +=
                  "\"" + std::string(fields.name) + "\":" +
                  JsWire<typename decltype(fields)::value_type>::read(reader) +
                  ","),
                 ...);
            },
            WireFields<T>::fields());
        return result + "})";
    }
};

template <typename F, std::size_t... I>
std::string emit_wire_binding_impl(std::uint32_t id,
                                   std::index_sequence<I...>) {
    using Traits = bindings::function_traits<std::decay_t<F>>;
    using Result = typename Traits::result_type;
    std::string parameters;
    std::string writes;
    ((parameters += (I ? ", " : "") + std::string("arg") + std::to_string(I),
      writes += JsWire<std::decay_t<typename Traits::template arg<I>>>::write(
          "request", "arg" + std::to_string(I))),
     ...);
    return "(" + parameters + ") => callTyped(" + std::to_string(id) +
           ", request => {" + writes + "}, response => " +
           JsWire<std::decay_t<Result>>::read("response") + ")";
}

template <typename F> std::string emit_wire_binding(std::uint32_t id) {
    using Traits = bindings::function_traits<std::decay_t<F>>;
    return emit_wire_binding_impl<F>(id,
                                     std::make_index_sequence<Traits::arity>{});
}

} // namespace app::binary_rpc
