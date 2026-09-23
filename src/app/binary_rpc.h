#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace app::binary_rpc {

using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t max_message_size = std::size_t{16} * 1024 * 1024;
constexpr unsigned bits_per_byte = 8;
constexpr unsigned word_bits = 32;

class WireError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {
        if (bytes.size() > max_message_size) {
            throw WireError("Message too large");
        }
    }

    [[nodiscard]] std::uint8_t u8() {
        require(1);
        return bytes_[offset_++];
    }

    [[nodiscard]] std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < word_bits; shift += bits_per_byte) {
            value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        }
        return value;
    }

    [[nodiscard]] std::int32_t i32() {
        const auto bits = u32();
        std::int32_t result = 0;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    [[nodiscard]] double f64() {
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        const auto low = static_cast<std::uint64_t>(u32());
        const auto high = static_cast<std::uint64_t>(u32());
        const auto bits = low | (high << word_bits);
        double result = 0;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes() {
        const auto size = u32();
        require(size);
        const auto result = bytes_.subspan(offset_, size);
        offset_ += size;
        return result;
    }

    [[nodiscard]] std::string string() {
        const auto data = bytes();
        std::string result(data.size(), '\0');
        if (!data.empty()) {
            std::memcpy(result.data(), data.data(), data.size());
        }
        return result;
    }

    [[nodiscard]] std::span<const std::uint8_t> remaining() {
        const auto result = bytes_.subspan(offset_);
        offset_ = bytes_.size();
        return result;
    }

    [[nodiscard]] std::size_t unread_size() const noexcept {
        return bytes_.size() - offset_;
    }

    void finish() const {
        if (offset_ != bytes_.size()) {
            throw WireError("Trailing bytes");
        }
    }

  private:
    void require(std::size_t count) const {
        if (count > bytes_.size() - offset_) {
            throw WireError("Truncated message");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

class Writer {
  public:
    void u8(std::uint8_t value) {
        if (data_.size() >= max_message_size) {
            throw WireError("Message too large");
        }
        data_.push_back(value);
    }

    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < word_bits; shift += bits_per_byte) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void i32(std::int32_t value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void f64(double value) {
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(static_cast<std::uint32_t>(bits));
        u32(static_cast<std::uint32_t>(bits >> word_bits));
    }

    void bytes(std::span<const std::uint8_t> value) {
        check_length(value.size());
        u32(static_cast<std::uint32_t>(value.size()));
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void raw(std::span<const std::uint8_t> value) {
        if (value.size() > max_message_size - data_.size()) {
            throw WireError("Message too large");
        }
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void string(std::string_view value) {
        check_length(value.size());
        u32(static_cast<std::uint32_t>(value.size()));
        for (const char byte : value) {
            data_.push_back(static_cast<std::uint8_t>(byte));
        }
    }

    [[nodiscard]] Bytes take() && { return std::move(data_); }

  private:
    void check_length(std::size_t size) const {
        if (size > max_message_size - 4 ||
            size > (std::numeric_limits<std::uint32_t>::max)() ||
            data_.size() > max_message_size - 4 - size) {
            throw WireError("Message too large");
        }
    }

    Bytes data_;
};

using Handler = std::function<void(Reader &, Writer &)>;

class Dispatcher {
  public:
    void bind(std::uint32_t id, Handler handler) {
        if (id == 0 || !handlers_.emplace(id, std::move(handler)).second) {
            throw WireError("Duplicate or invalid method ID");
        }
    }

    [[nodiscard]] Bytes call(std::uint32_t id,
                             std::span<const std::uint8_t> request) const {
        return call_impl(id, request, false);
    }

    // The transport response starts with a success byte. Writing it before
    // the handler avoids copying large replies to prepend the envelope.
    [[nodiscard]] Bytes call_enveloped(
        std::uint32_t id, std::span<const std::uint8_t> request) const {
        return call_impl(id, request, true);
    }

  private:
    [[nodiscard]] Bytes call_impl(
        std::uint32_t id, std::span<const std::uint8_t> request,
        bool enveloped) const {
        auto found = handlers_.find(id);
        if (found == handlers_.end()) {
            throw WireError("Unknown method ID");
        }
        Reader reader(request);
        Writer writer;
        if (enveloped) {
            writer.u8(0);
        }
        found->second(reader, writer);
        reader.finish();
        return std::move(writer).take();
    }

    std::map<std::uint32_t, Handler> handlers_;
};

// Error envelope: u8(1), u32(code), UTF-8 string(message).
[[nodiscard]] inline Bytes error_response(std::uint32_t code,
                                          std::string_view message) {
    Writer writer;
    writer.u8(1);
    writer.u32(code);
    writer.string(message);
    return std::move(writer).take();
}

} // namespace app::binary_rpc
