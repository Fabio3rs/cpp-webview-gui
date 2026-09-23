#include <gtest/gtest.h>

#include "app/binary_rpc_bindings.h"
#include "app/binary_rpc_transport.h"
#include "app/handlers.h"
#include <glib.h>

#include <string>

TEST(BinaryBindings, CborDispatchesExistingTypedHandler) {
    app::binary_rpc::Dispatcher dispatcher;
    app::binary_rpc::bind_cbor(
        dispatcher, "greet", [](const std::string &name) {
            return app::bindings::json{{"message", "Olá, " + name}};
        });
    const auto args = app::bindings::json::array({"Fabio"});
    const auto request = app::bindings::json::to_cbor(args);
    const auto response =
        dispatcher.call(app::binary_rpc::method_id("greet"), request);
    const auto decoded = app::bindings::json::from_cbor(response);
    EXPECT_TRUE(decoded.at("ok").get<bool>());
    EXPECT_EQ(decoded.at("data").at("message"), "Olá, Fabio");
}

TEST(BinaryBindings, CborPreservesBindingErrors) {
    app::binary_rpc::Dispatcher dispatcher;
    app::binary_rpc::bind_cbor(dispatcher, "needsNumber",
                               [](int value) { return value; });
    const auto args = app::bindings::json::array({"wrong"});
    const auto response =
        dispatcher.call(app::binary_rpc::method_id("needsNumber"),
                        app::bindings::json::to_cbor(args));
    const auto decoded = app::bindings::json::from_cbor(response);
    EXPECT_FALSE(decoded.at("ok").get<bool>());
    EXPECT_EQ(decoded.at("error").at("code"), 400);
}

TEST(BinaryWebview, FetchTransfersTypedBytes) {
    app::binary_rpc::Dispatcher dispatcher;
    dispatcher.bind(7,
                    [](auto &in, auto &out) { out.i32(in.i32() + in.i32()); });
    dispatcher.bind(8, [](auto &in, auto &out) { out.bytes(in.bytes()); });
    app::binary_rpc::bind_cbor(dispatcher, "sumCbor", [](int left, int right) {
        return left + right;
    });

    webview::webview window(false, nullptr);
    app::HandlerRegistry handlers;
    app::setup(window, handlers, &dispatcher);
    if (!app::binary_rpc::install_transport(window, dispatcher)) {
        GTEST_SKIP() << "WebKitGTK before 2.40 has no binary POST body API";
    }

    std::string result = "timeout";
    window.bind("report", [&window, &result](const std::string &value) {
        result = value;
        window.terminate();
        return std::string("null");
    });

    const auto timeout = g_timeout_add_seconds(
        10,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &window);

    const char *html = R"html(<!doctype html><script>
      const payload = new Uint8Array(8);
      const input = new DataView(payload.buffer);
      input.setInt32(0, 20, true);
      input.setInt32(4, 22, true);
      fetch('app-rpc://native/7', {
        method: 'POST',
        body: payload,
        headers: {'Content-Type': 'application/octet-stream'}
      }).then(async response => {
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const result = new DataView(await response.arrayBuffer());
        if (result.getInt32(0, true) !== 42) throw new Error('wrong integer');
        const count = 15 * 1024 * 1024 - 4;
        const large = new Uint8Array(count + 4);
        new DataView(large.buffer).setUint32(0, count, true);
        large.fill(0xab, 4);
        const echo = await fetch('app-rpc://native/8', {
          method: 'POST',
          body: large,
          headers: {'Content-Type': 'application/octet-stream'}
        });
        if (!echo.ok) throw new Error('echo HTTP ' + echo.status);
        const echoed = new Uint8Array(await echo.arrayBuffer());
        const size = new DataView(echoed.buffer).getUint32(0, true);
        if (size !== count || echoed.length !== large.length ||
            echoed[4] !== 0xab || echoed[echoed.length - 1] !== 0xab) {
          throw new Error('wrong payload');
        }
        let hash = 2166136261;
        for (const byte of new TextEncoder().encode('sumCbor')) {
          hash = Math.imul(hash ^ byte, 16777619) >>> 0;
        }
        const cbor = await fetch(`app-rpc://native/${hash}`, {
          method: 'POST',
          body: Uint8Array.of(0x82, 0x14, 0x16),
          headers: {'Content-Type': 'application/octet-stream'}
        });
        if (!cbor.ok) throw new Error('CBOR HTTP ' + cbor.status);
        const actual = new Uint8Array(await cbor.arrayBuffer());
        const expected = Uint8Array.of(
          0xa2, 0x64, 0x64, 0x61, 0x74, 0x61, 0x18, 0x2a,
          0x62, 0x6f, 0x6b, 0xf5);
        if (actual.length !== expected.length ||
            !actual.every((byte, index) => byte === expected[index])) {
          throw new Error('wrong CBOR');
        }
        let pingHash = 2166136261;
        for (const byte of new TextEncoder().encode('ping')) {
          pingHash = Math.imul(pingHash ^ byte, 16777619) >>> 0;
        }
        const ping = await fetch(`app-rpc://native/${pingHash}`, {
          method: 'POST',
          body: Uint8Array.of(0x81, 0x62, 0x68, 0x69),
          headers: {'Content-Type': 'application/octet-stream'}
        });
        const text = new TextDecoder().decode(await ping.arrayBuffer());
        report(ping.ok && text.includes('pong') && text.includes('hi')
          ? 'ok' : 'wrong named binding');
      }).catch(error => report(String(error)));
    </script>)html";
    app::binary_rpc::load_html_with_binary_origin(window, html);
    window.run();
    if (result != "timeout") {
        g_source_remove(timeout);
    }
    EXPECT_EQ(result, R"(["ok"])");
}
