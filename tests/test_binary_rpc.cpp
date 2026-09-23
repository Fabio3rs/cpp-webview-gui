#include "app/binary_rpc.h"
#include "app/binding_policy.h"
#include "app/navigation_policy.h"
#include "app/wire_codec.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace rpc = app::binary_rpc;

TEST(BindingPolicy, ExternalProductionUrlHasNoNativeBindings) {
    EXPECT_TRUE(app::should_install_bindings(""));
    EXPECT_FALSE(app::should_install_bindings("https://example.invalid/page"));
    EXPECT_FALSE(app::should_install_bindings("http://localhost:5173"));
}

TEST(NavigationPolicy, AcceptsOnlyTheConfiguredOrigin) {
    const auto trusted =
        app::parse_trusted_origin("http://127.0.0.1:5173/index.html");
    ASSERT_TRUE(trusted.has_value());
    EXPECT_TRUE(app::is_trusted_navigation(
        "HTTP://127.0.0.1:5173/page?wid=w1", *trusted));
    EXPECT_FALSE(app::is_trusted_navigation(
        "http://127.0.0.1:5174/page", *trusted));
    EXPECT_FALSE(app::is_trusted_navigation(
        "http://127.0.0.1:5173.evil.invalid/page", *trusted));
    EXPECT_FALSE(app::is_trusted_navigation(
        "http://127.0.0.1:5173@evil.invalid/page", *trusted));
    EXPECT_FALSE(app::is_trusted_navigation(
        "http://127.0.0.1:5173\\@evil.invalid/page", *trusted));
    EXPECT_FALSE(app::is_trusted_navigation("about:blank", *trusted));
    EXPECT_FALSE(app::is_trusted_navigation("data:text/html,hello", *trusted));
}

TEST(NavigationPolicy, NormalizesDefaultPortsAndCustomScheme) {
    const auto https = app::parse_trusted_origin("https://example.invalid/");
    ASSERT_TRUE(https.has_value());
    EXPECT_TRUE(app::is_trusted_navigation(
        "https://EXAMPLE.invalid:443/next", *https));
    EXPECT_FALSE(app::is_trusted_navigation(
        "http://example.invalid/next", *https));

    const auto scheme =
        app::parse_trusted_origin("app-rpc://native/index.html");
    ASSERT_TRUE(scheme.has_value());
    EXPECT_TRUE(app::is_trusted_navigation("app-rpc://native/next", *scheme));
    EXPECT_FALSE(app::is_trusted_navigation(
        "app-rpc://native.evil.invalid/next", *scheme));
}

TEST(NavigationPolicy, OpensOnlyUserClickedExternalWebLinks) {
    const auto trusted = app::parse_trusted_origin("app-rpc://native/");
    ASSERT_TRUE(trusted.has_value());
    using Decision = app::NavigationDecision;
    EXPECT_EQ(app::decide_navigation("app-rpc://native/page", *trusted,
                                     false, false, false),
              Decision::allow);
    EXPECT_EQ(app::decide_navigation("https://example.invalid/", *trusted,
                                     false, true, true),
              Decision::open_external);
    EXPECT_EQ(app::decide_navigation("https://example.invalid/", *trusted,
                                     true, true, true),
              Decision::open_external);
    EXPECT_EQ(app::decide_navigation("https://example.invalid/", *trusted,
                                     false, false, true),
              Decision::deny);
    EXPECT_EQ(app::decide_navigation("https://example.invalid/", *trusted,
                                     false, true, false),
              Decision::deny);
    EXPECT_EQ(app::decide_navigation("file:///tmp/file", *trusted, false,
                                     true, true),
              Decision::deny);
    EXPECT_EQ(app::decide_navigation("app-rpc://native/page", *trusted,
                                     true, true, true),
              Decision::deny);
    EXPECT_EQ(app::decide_opaque_navigation("https://example.invalid/", true,
                                            true),
              Decision::open_external);
    EXPECT_EQ(app::decide_opaque_navigation("about:blank", true, true),
              Decision::deny);
}

TEST(NavigationPolicy, EmbeddedDocumentAllowsOnlyItsInitialBlankNavigation) {
    app::NavigationSession session(std::nullopt);
    using Decision = app::NavigationDecision;
    EXPECT_EQ(session.decide("about:blank", false, false, false, false),
              Decision::deny);
    EXPECT_EQ(session.decide("about:blank", true, false, false, false),
              Decision::allow);
    EXPECT_EQ(session.decide("about:blank", true, false, false, false),
              Decision::deny);
    EXPECT_EQ(session.decide("https://example.invalid/", true, false, false,
                             false),
              Decision::deny);
    EXPECT_EQ(session.decide("https://example.invalid/", true, true, true,
                             true),
              Decision::open_external);

    app::NavigationSession empty_url_session(std::nullopt);
    EXPECT_EQ(empty_url_session.decide("", true, false, false, false),
              Decision::allow);
    EXPECT_EQ(empty_url_session.decide("", true, false, false, false),
              Decision::deny);
}

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

TEST(BinaryDispatcher, EnvelopesTransportResponsesWithoutChangingPayload) {
    rpc::Dispatcher dispatcher;
    dispatcher.bind(7, [](rpc::Reader &, rpc::Writer &writer) {
        writer.i32(42);
    });
    const auto response = dispatcher.call_enveloped(7, {});
    rpc::Reader reader(response);
    EXPECT_EQ(reader.u8(), 0);
    EXPECT_EQ(reader.i32(), 42);
    EXPECT_NO_THROW(reader.finish());

    const auto error = rpc::error_response(400, "Invalid input");
    rpc::Reader error_reader(error);
    EXPECT_EQ(error_reader.u8(), 1);
    EXPECT_EQ(error_reader.u32(), 400U);
    EXPECT_EQ(error_reader.string(), "Invalid input");
    EXPECT_NO_THROW(error_reader.finish());
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
