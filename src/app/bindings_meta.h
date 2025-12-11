#pragma once
// Metadata for native JS bindings - registry for TS generation and location
// index

#include "app/bindings.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <ostream>
#include <source_location>
#include <string>
#include <vector>

namespace app::bindings::meta {

using json = nlohmann::json;

struct CppLocation {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct BindingMeta {
    std::string name;
    std::string return_ts;
    std::vector<std::string> args_ts;
    // Support a begin/end range so tools can map to an implementation span
    CppLocation cpp_begin;
    CppLocation cpp_end;
};

inline std::vector<BindingMeta> &registry() {
    static std::vector<BindingMeta> r;
    return r;
}

// Minimal TS type mapping - extend as needed
template <typename T> struct TsType {
    static std::string name() { return "any"; }
};

template <> struct TsType<void> {
    static std::string name() { return "void"; }
};
template <> struct TsType<bool> {
    static std::string name() { return "boolean"; }
};
template <> struct TsType<int> {
    static std::string name() { return "number"; }
};
template <> struct TsType<long> {
    static std::string name() { return "number"; }
};
template <> struct TsType<double> {
    static std::string name() { return "number"; }
};
template <> struct TsType<float> {
    static std::string name() { return "number"; }
};
template <> struct TsType<std::string> {
    static std::string name() { return "string"; }
};
template <> struct TsType<json> {
    static std::string name() { return "any"; }
};

template <typename T> struct TsType<std::optional<T>> {
    static std::string name() {
        return TsType<std::decay_t<T>>::name() + " | null";
    }
};

template <typename F, std::size_t... I>
inline void fill_arg_types(std::vector<std::string> &out,
                           std::index_sequence<I...>) {
    using traits = function_traits<std::decay_t<F>>;
    (out.push_back(
         TsType<std::decay_t<typename traits::template arg<I>>>::name()),
     ...);
}

template <typename F>
inline void register_binding_meta(
    std::string_view jsName,
    std::source_location begin = std::source_location::current(),
    std::source_location end = std::source_location::current()) {
    using traits = function_traits<std::decay_t<F>>;
    BindingMeta meta;
    meta.name = std::string(jsName);
    meta.return_ts = TsType<std::decay_t<typename traits::result_type>>::name();
    meta.args_ts.reserve(traits::arity);
    fill_arg_types<F>(meta.args_ts, std::make_index_sequence<traits::arity>{});
    meta.cpp_begin.file = begin.file_name();
    meta.cpp_begin.line = static_cast<std::uint32_t>(begin.line());
    meta.cpp_begin.column = static_cast<std::uint32_t>(begin.column());

    meta.cpp_end.file = end.file_name();
    meta.cpp_end.line = static_cast<std::uint32_t>(end.line());
    meta.cpp_end.column = static_cast<std::uint32_t>(end.column());
    registry().push_back(std::move(meta));
}

inline void dump_typescript_and_index(std::ostream &dts,
                                      std::ostream &json_out) {
    const auto &regs = registry();
    dts << "export {};\n\n";
    dts << "declare global {\n";
    for (const auto &b : regs) {
        dts << "  function " << b.name << "(";
        for (std::size_t i = 0; i < b.args_ts.size(); ++i) {
            dts << "arg" << i << ": " << b.args_ts[i];
            if (i + 1 < b.args_ts.size())
                dts << ", ";
        }
        dts << "): " << b.return_ts << ";\n";
    }
    dts << "}\n";

    json idx = json::object();
    for (const auto &b : regs) {
        json loc = json::object();
        loc["begin"] = {{"file", b.cpp_begin.file},
                        {"line", b.cpp_begin.line},
                        {"column", b.cpp_begin.column}};
        loc["end"] = {{"file", b.cpp_end.file},
                      {"line", b.cpp_end.line},
                      {"column", b.cpp_end.column}};
        idx[b.name] = loc;
    }
    json_out << idx.dump(2);
}

} // namespace app::bindings::meta
