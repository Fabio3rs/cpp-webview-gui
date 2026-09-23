#pragma once

#include "app/binary_rpc.h"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace app::binary_rpc {

// A specialization defines the binary representation of one C++ value.
template <typename T> struct WireCodec;

template <> struct WireCodec<bool> {
    static bool read(Reader &reader) {
        const auto value = reader.u8();
        if (value > 1) {
            throw WireError("Invalid boolean");
        }
        return value == 1;
    }
    static void write(Writer &writer, bool value) { writer.u8(value ? 1 : 0); }
};

template <> struct WireCodec<std::int32_t> {
    static std::int32_t read(Reader &reader) { return reader.i32(); }
    static void write(Writer &writer, std::int32_t value) { writer.i32(value); }
};

template <> struct WireCodec<std::uint32_t> {
    static std::uint32_t read(Reader &reader) { return reader.u32(); }
    static void write(Writer &writer, std::uint32_t value) { writer.u32(value); }
};

template <> struct WireCodec<double> {
    static double read(Reader &reader) { return reader.f64(); }
    static void write(Writer &writer, double value) { writer.f64(value); }
};

template <> struct WireCodec<std::string> {
    static std::string read(Reader &reader) { return reader.string(); }
    static void write(Writer &writer, const std::string &value) {
        writer.string(value);
    }
};

template <> struct WireCodec<Bytes> {
    static Bytes read(Reader &reader) {
        const auto bytes = reader.bytes();
        return Bytes(bytes.begin(), bytes.end());
    }
    static void write(Writer &writer, const Bytes &value) {
        writer.bytes(value);
    }
};

template <typename T> struct WireCodec<std::optional<T>> {
    static std::optional<T> read(Reader &reader) {
        if (!WireCodec<bool>::read(reader)) {
            return std::nullopt;
        }
        return WireCodec<T>::read(reader);
    }
    static void write(Writer &writer, const std::optional<T> &value) {
        WireCodec<bool>::write(writer, value.has_value());
        if (value) {
            WireCodec<T>::write(writer, *value);
        }
    }
};

template <typename T> struct WireCodec<std::vector<T>> {
    static std::vector<T> read(Reader &reader) {
        const auto count = reader.u32();
        if (count > max_message_size || count > reader.unread_size()) {
            throw WireError("Invalid vector length");
        }
        std::vector<T> values;
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            values.push_back(WireCodec<T>::read(reader));
        }
        return values;
    }
    static void write(Writer &writer, const std::vector<T> &values) {
        if (values.size() > max_message_size) {
            throw WireError("Vector too large");
        }
        writer.u32(static_cast<std::uint32_t>(values.size()));
        for (const auto &value : values) {
            WireCodec<T>::write(writer, value);
        }
    }
};

} // namespace app::binary_rpc
