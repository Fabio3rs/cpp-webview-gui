#include "app/binary_rpc.h"
#include "app/wire_codec.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace rpc = app::binary_rpc;

TEST(BinaryWire, RoundTripsPrimitiveValues) {
    rpc::Writer writer;
    writer.u8(1);
    writer.i32(-123456);
    writer.f64(3.14159);
    writer.string("olá");
    const auto encoded = std::move(writer).take();
    rpc::Reader reader(encoded);
    EXPECT_EQ(reader.u8(), 1);
    EXPECT_EQ(reader.i32(), -123456);
    EXPECT_DOUBLE_EQ(reader.f64(), 3.14159);
    EXPECT_EQ(reader.string(), "olá");
    EXPECT_NO_THROW(reader.finish());
}

TEST(BinaryWire, RejectsTruncatedAndTrailingMessages) {
    const std::array<std::uint8_t, 3> truncated{4, 0, 0};
    rpc::Reader reader(truncated);
    EXPECT_THROW(static_cast<void>(reader.u32()), rpc::WireError);

    const std::array<std::uint8_t, 1> trailing{1};
    rpc::Reader other(trailing);
    EXPECT_THROW(other.finish(), rpc::WireError);
}

TEST(BinaryWire, RoundTripsLargeBinaryPayload) {
    rpc::Bytes payload(15U * 1024U * 1024U - 4U, 0xab);
    rpc::Writer writer;
    writer.bytes(payload);
    auto encoded = std::move(writer).take();
    EXPECT_EQ(encoded.size(), 15U * 1024U * 1024U);
    rpc::Reader reader(encoded);
    const auto decoded = reader.bytes();
    ASSERT_EQ(decoded.size(), payload.size());
    EXPECT_EQ(decoded.front(), 0xab);
    EXPECT_EQ(decoded.back(), 0xab);
    EXPECT_NO_THROW(reader.finish());
}

TEST(BinaryWire, RejectsOversizedPayload) {
    rpc::Bytes payload(rpc::max_message_size, 0);
    rpc::Writer writer;
    EXPECT_THROW(writer.bytes(payload), rpc::WireError);
}

TEST(BinaryDispatcher, DispatchesByStableNumericId) {
    rpc::Dispatcher dispatcher;
    dispatcher.bind(7, [](rpc::Reader &reader, rpc::Writer &writer) {
        writer.i32(reader.i32() + 1);
    });
    rpc::Writer request;
    request.i32(41);
    const auto response = dispatcher.call(7, std::move(request).take());
    rpc::Reader reader(response);
    EXPECT_EQ(reader.i32(), 42);
    EXPECT_NO_THROW(reader.finish());
}

TEST(BinaryDispatcher, RejectsUnknownAndExtraArguments) {
    rpc::Dispatcher dispatcher;
    dispatcher.bind(7, [](rpc::Reader &, rpc::Writer &) {});
    const std::array<std::uint8_t, 1> extra{1};
    EXPECT_THROW(static_cast<void>(dispatcher.call(7, extra)), rpc::WireError);
    EXPECT_THROW(static_cast<void>(dispatcher.call(8, {})), rpc::WireError);
    EXPECT_THROW(dispatcher.bind(7, [](auto &, auto &) {}), rpc::WireError);
}

TEST(BinaryWireCodec, ComposesOptionalVectorAndUtf8) {
    rpc::Writer writer;
    rpc::WireCodec<std::optional<std::string>>::write(writer, "olá");
    rpc::WireCodec<std::vector<std::int32_t>>::write(writer, {1, -2, 3});
    rpc::WireCodec<std::optional<std::string>>::write(writer, std::nullopt);
    const auto encoded = std::move(writer).take();
    rpc::Reader reader(encoded);
    EXPECT_EQ(rpc::WireCodec<std::optional<std::string>>::read(reader), "olá");
    EXPECT_EQ(rpc::WireCodec<std::vector<std::int32_t>>::read(reader),
              (std::vector<std::int32_t>{1, -2, 3}));
    EXPECT_EQ(rpc::WireCodec<std::optional<std::string>>::read(reader),
              std::nullopt);
    EXPECT_NO_THROW(reader.finish());
}

TEST(BinaryWireCodec, RejectsInvalidBooleanAndVectorLength) {
    const std::array<std::uint8_t, 1> invalid_boolean{2};
    rpc::Reader boolean_reader(invalid_boolean);
    EXPECT_THROW(static_cast<void>(rpc::WireCodec<bool>::read(boolean_reader)),
                 rpc::WireError);

    rpc::Writer writer;
    writer.u32(100);
    const auto encoded = std::move(writer).take();
    rpc::Reader vector_reader(encoded);
    EXPECT_THROW(
        static_cast<void>(rpc::WireCodec<std::vector<std::int32_t>>::read(
            vector_reader)),
        rpc::WireError);
}
