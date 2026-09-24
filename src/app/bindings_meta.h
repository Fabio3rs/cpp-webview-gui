#pragma once
// Metadata for native JS bindings - registry for TS generation and location
// index

#include "app/binary_rpc_bindings.h"
#include "app/bindings.h"
#include "app/wire_codec.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <ostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
    std::string wire_js;
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
    static std::string name() { return "Record<string, never>"; }
};
template <> struct TsType<bool> {
    static std::string name() { return "boolean"; }
};
template <> struct TsType<int> {
    static std::string name() { return "number"; }
};
template <> struct TsType<unsigned int> {
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

template <typename T> struct TsType<std::vector<T>> {
    static std::string name() {
        return "Array<" + TsType<std::decay_t<T>>::name() + ">";
    }
};
template <> struct TsType<binary_rpc::Bytes> {
    static std::string name() { return "Uint8Array"; }
};

template <binary_rpc::HasWireFields T> struct TsType<T> {
    static std::string name() {
        std::string result = "{ ";
        bool first = true;
        std::apply(
            [&](auto... fields) {
                ((result +=
                  (first ? "" : "; ") + std::string(fields.name) + ": " +
                  TsType<typename decltype(fields)::value_type>::name(),
                  first = false),
                 ...);
            },
            binary_rpc::WireFields<T>::fields());
        return result + " }";
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
    std::source_location end = std::source_location::current(),
    std::string wire_js = {}) {
    using traits = function_traits<std::decay_t<F>>;
    BindingMeta meta;
    meta.name = std::string(jsName);
    meta.return_ts = TsType<std::decay_t<typename traits::result_type>>::name();
    meta.args_ts.reserve(traits::arity);
    meta.wire_js = std::move(wire_js);
    fill_arg_types<F>(meta.args_ts, std::make_index_sequence<traits::arity>{});
    meta.cpp_begin.file = begin.file_name();
    meta.cpp_begin.line = static_cast<std::uint32_t>(begin.line());
    meta.cpp_begin.column = static_cast<std::uint32_t>(begin.column());

    meta.cpp_end.file = end.file_name();
    meta.cpp_end.line = static_cast<std::uint32_t>(end.line());
    meta.cpp_end.column = static_cast<std::uint32_t>(end.column());
    const auto existing =
        std::find_if(registry().begin(), registry().end(),
                     [&](const auto &item) { return item.name == meta.name; });
    if (existing != registry().end()) {
        if ((!existing->wire_js.empty() && !meta.wire_js.empty() &&
             existing->wire_js != meta.wire_js) ||
            existing->return_ts != meta.return_ts ||
            existing->args_ts != meta.args_ts) {
            throw std::runtime_error("Conflicting binding contract: " +
                                     meta.name);
        }
        if (existing->wire_js.empty()) {
            existing->wire_js = std::move(meta.wire_js);
        }
        return;
    }
    registry().push_back(std::move(meta));
}

inline void dump_javascript(std::ostream &out) {
    auto bindings = registry();
    std::sort(bindings.begin(), bindings.end(),
              [](const auto &left, const auto &right) {
                  return left.name < right.name;
              });
    std::unordered_map<std::uint32_t, std::string> ids;
    for (const auto &binding : bindings) {
        const auto [found, inserted] =
            ids.emplace(binary_rpc::method_id(binding.name), binding.name);
        if (!inserted && found->second != binding.name) {
            throw std::runtime_error("Wire method ID collision: " +
                                     found->second + " and " + binding.name);
        }
    }
    out << "// Generated by emit_native_bindings. Do not edit.\n"
           "import { callTyped, installBinaryBindings, readWireBool } "
           "from '../binary_rpc.js'\n"
           "import { readBootstrap, readOpaque, readOutsideDrop, "
           "writeBootstrap, writeOpaque } from '../native_wire_types.js'\n\n"
           "export const nativeBindings = {\n";
    for (const auto &binding : bindings) {
        if (binding.wire_js.empty()) {
            throw std::runtime_error("Missing wire schema for binding " +
                                     binding.name);
        }
        out << "    " << json(binding.name).dump() << ": " << binding.wire_js
            << ",\n";
    }
    out << "}\n\n"
           "export const nativeBindingNames = "
           "Object.keys(nativeBindings).sort()\n"
           "export function installNativeBindings() {\n"
           "    installBinaryBindings(nativeBindings)\n"
           "}\n";
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
inline void
dump_typescript_and_index(std::ostream &dts, std::ostream &json_out,
                          const std::filesystem::path &index_root = {}) {
    const auto &regs = registry();
    dts << "export function installNativeBindings(): void;\n";
    dts << "export const nativeBindingNames: string[];\n\n";
    dts << "declare global {\n";
    dts << "  type NativeBindingResult<T> = "
           "{ ok: true; data?: T } | "
           "{ ok: false; error: { code: number; message: string } };\n";
    for (const auto &b : regs) {
        dts << "  function " << b.name << "(";
        for (std::size_t i = 0; i < b.args_ts.size(); ++i) {
            dts << "arg" << i << ": " << b.args_ts[i];
            if (i + 1 < b.args_ts.size())
                dts << ", ";
        }
        dts << "): Promise<NativeBindingResult<" << b.return_ts << ">>;\n";
    }
    dts << "}\n";

    json idx = json::object();
    const auto output_path = [&](const std::string &file) {
        if (index_root.empty()) {
            return file;
        }
        std::error_code error;
        const auto relative =
            std::filesystem::relative(file, index_root, error);
        return error ? file : relative.generic_string();
    };
    for (const auto &b : regs) {
        json loc = json::object();
        loc["begin"] = {{"file", output_path(b.cpp_begin.file)},
                        {"line", b.cpp_begin.line},
                        {"column", b.cpp_begin.column}};
        loc["end"] = {{"file", output_path(b.cpp_end.file)},
                      {"line", b.cpp_end.line},
                      {"column", b.cpp_end.column}};
        idx[b.name] = loc;
    }
    json_out << idx.dump(2);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace app::bindings::meta
