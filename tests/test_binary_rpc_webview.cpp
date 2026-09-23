#include <gtest/gtest.h>

#include "app/binary_rpc_bindings.h"
#include "app/binary_rpc_transport.h"
#include "app/handlers.h"
#include "app/navigation_guard.h"
#include "app/window_manager.h"
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

TEST(BinaryBindings, WireBindingCallsTypedHandlerWithoutJson) {
    app::binary_rpc::Dispatcher dispatcher;
    app::binary_rpc::bind_wire(dispatcher, "add", [](int left, int right) {
        return left + right;
    });
    app::binary_rpc::Writer request;
    request.i32(20);
    request.i32(22);
    const auto response = dispatcher.call(
        app::binary_rpc::method_id("add"), std::move(request).take());
    app::binary_rpc::Reader reader(response);
    EXPECT_EQ(reader.i32(), 42);
    EXPECT_NO_THROW(reader.finish());
}

TEST(BinaryBindings, PingWireKeepsTheLegacyResponseShape) {
    app::binary_rpc::Dispatcher dispatcher;
    app::HandlerRegistry handlers;
    app::binary_rpc::bind_wire(
        dispatcher, "ping", [&handlers](std::optional<std::string> message) {
            return handlers.ping(message);
        });
    app::binary_rpc::Writer request;
    app::binary_rpc::WireCodec<std::optional<std::string>>::write(request,
                                                                 "hello");
    const auto response = dispatcher.call(
        app::binary_rpc::method_id("ping"), std::move(request).take());
    app::binary_rpc::Reader reader(response);
    const auto value = app::binary_rpc::WireCodec<app::PingResult>::read(reader);
    EXPECT_EQ(value.message, "pong");
    EXPECT_EQ(value.echo, "hello");
    EXPECT_NO_THROW(reader.finish());
    EXPECT_EQ(app::bindings::JsConv<app::PingResult>::to_json(value),
              (app::bindings::json{{"message", "pong"}, {"echo", "hello"}}));
}

TEST(BinaryBindings, BootstrapPreservesTypedSettingsAndOpaqueUiState) {
    const app::bindings::json original = {
        {"title", "Inspector"},
        {"width", 840},
        {"left", 17},
        {"kind", "dockview"},
        {"panels", {{{"component", "InspectorPanel"},
                     {"params", {{"nested", {1, true}}}}}}}};
    auto value = app::bindings::JsConv<app::WindowBootstrap>::from_json(original);
    ASSERT_EQ(value.title, "Inspector");
    ASSERT_EQ(value.width, 840.0);
    app::binary_rpc::Writer writer;
    app::binary_rpc::WireCodec<app::WindowBootstrap>::write(writer, value);
    const auto encoded = std::move(writer).take();
    app::binary_rpc::Reader reader(encoded);
    const auto decoded =
        app::binary_rpc::WireCodec<app::WindowBootstrap>::read(reader);
    reader.finish();
    EXPECT_EQ(app::bindings::JsConv<app::WindowBootstrap>::to_json(decoded),
              original);
}

TEST(BinaryBindings, WindowAndDragResultsUseTypedWireValues) {
    app::binary_rpc::Dispatcher dispatcher;
    app::binary_rpc::bind_wire(dispatcher, "listNativeWindows", []() {
        return std::vector<app::NativeWindowInfo>{{"main", "Main"},
                                                   {"w1", "Inspector"}};
    });
    const auto bytes = dispatcher.call(
        app::binary_rpc::method_id("listNativeWindows"), {});
    app::binary_rpc::Reader reader(bytes);
    const auto windows =
        app::binary_rpc::WireCodec<std::vector<app::NativeWindowInfo>>::read(
            reader);
    ASSERT_EQ(windows.size(), 2U);
    EXPECT_EQ(windows[1].title, "Inspector");
    reader.finish();

    const app::OutsideDrop result{
        app::bindings::JsConv<app::OpaqueValue>::from_json(
            {{"panels", {{{"id", "one"}}}}}),
        app::DropPoint{12.5, 7.5}};
    app::binary_rpc::Writer output;
    app::binary_rpc::WireCodec<std::optional<app::OutsideDrop>>::write(
        output, result);
    const auto encoded = std::move(output).take();
    app::binary_rpc::Reader reply(encoded);
    const auto decoded =
        app::binary_rpc::WireCodec<std::optional<app::OutsideDrop>>::read(reply);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(app::bindings::JsConv<app::OutsideDrop>::to_json(*decoded),
              app::bindings::JsConv<app::OutsideDrop>::to_json(result));
    reply.finish();
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
    ASSERT_TRUE(app::install_navigation_guard(
        window, "app-rpc://native/index.html"));
    app::bindings::remove_legacy_bindings(window);

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
      const legacyExposed = typeof window.ping !== 'undefined';
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
        if (result.getUint8(0) !== 0 || result.getInt32(1, true) !== 42)
          throw new Error('wrong integer');
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
        const size = new DataView(echoed.buffer).getUint32(1, true);
        if (echoed[0] !== 0 || size !== count ||
            echoed.length !== large.length + 1 ||
            !echoed.subarray(5).every(byte => byte === 0xab)) {
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
        const actual = new Uint8Array(await cbor.arrayBuffer()).subarray(1);
        const expected = Uint8Array.of(
          0xa2, 0x64, 0x64, 0x61, 0x74, 0x61, 0x18, 0x2a,
          0x62, 0x6f, 0x6b, 0xf5);
        if (actual.length !== expected.length ||
            !actual.every((byte, index) => byte === expected[index])) {
          throw new Error('wrong CBOR');
        }
        const hashName = name => {
          let hash = 2166136261;
          for (const byte of new TextEncoder().encode(name)) {
            hash = Math.imul(hash ^ byte, 16777619) >>> 0;
          }
          return hash;
        };
        const readString = (buffer, offset) => {
          const length = new DataView(buffer).getUint32(offset, true);
          return {
            value: new TextDecoder().decode(new Uint8Array(buffer, offset + 4, length)),
            next: offset + 4 + length
          };
        };
        const pingRequest = new Uint8Array(7);
        pingRequest[0] = 1;
        new DataView(pingRequest.buffer).setUint32(1, 2, true);
        pingRequest.set(new TextEncoder().encode('hi'), 5);
        const ping = await fetch(`app-rpc://native/${hashName('ping')}`, {
          method: 'POST',
          body: pingRequest,
          headers: {'Content-Type': 'application/octet-stream'}
        });
        const pingBytes = await ping.arrayBuffer();
        const first = readString(pingBytes, 1);
        const second = readString(pingBytes, first.next);
        const config = await fetch(`app-rpc://native/${hashName('getConfig')}`,
          {method: 'POST', body: new Uint8Array()});
        const configBytes = await config.arrayBuffer();
        const theme = readString(configBytes, 1);
        const lang = readString(configBytes, theme.next);
        const version = await fetch(`app-rpc://native/${hashName('getVersion')}`,
          {method: 'POST', body: new Uint8Array()});
        const versionText = readString(await version.arrayBuffer(), 1).value;
        const emptyPath = new Uint8Array(4);
        const invalidFile = await fetch(`app-rpc://native/${hashName('openFile')}`,
          {method: 'POST', body: emptyPath});
        const errorBytes = await invalidFile.arrayBuffer();
        const errorCode = new DataView(errorBytes).getUint32(1, true);
        const errorText = readString(errorBytes, 5).value;
        const unknown = await fetch('app-rpc://native/4294967295',
          {method: 'POST', body: new Uint8Array()});
        const unknownBytes = await unknown.arrayBuffer();
        const unknownView = new DataView(unknownBytes);
        const extra = await fetch('app-rpc://native/7',
          {method: 'POST', body: Uint8Array.of(20, 0, 0, 0, 22, 0, 0, 0, 9)});
        const extraBytes = await extra.arrayBuffer();
        const extraView = new DataView(extraBytes);
        report(!legacyExposed && ping.ok &&
          first.value === 'pong' && second.value === 'hi' &&
          config.ok && theme.value === 'dark' && lang.value === 'pt-br' &&
          version.ok && versionText.length > 0 &&
          invalidFile.ok && new DataView(errorBytes).getUint8(0) === 1 &&
          errorCode === 400 && errorText.includes('Path not provided') &&
          unknown.ok && unknownView.getUint8(0) === 1 &&
          unknownView.getUint32(1, true) === 400 &&
          extra.ok && extraView.getUint8(0) === 1 &&
          extraView.getUint32(1, true) === 400
          ? 'ok' : 'wrong typed binding');
      }).catch(error => report(String(error)));
    </script>)html";
    app::binary_rpc::load_html_with_binary_origin(window, html);
    window.run();
    if (result != "timeout") {
        g_source_remove(timeout);
    }
    EXPECT_EQ(result, R"(["ok"])");
    app::binary_rpc::clear_transport(window);
}

TEST(BinaryWebview, SecondWindowSharesBinaryTransport) {
    app::binary_rpc::Dispatcher dispatcher;
    app::binary_rpc::bind_wire(dispatcher, "secondOnly", []() { return 42; });
    webview::webview main(false, nullptr);
    if (!app::binary_rpc::install_transport(main, std::move(dispatcher))) {
        GTEST_SKIP() << "WebKitGTK before 2.40 has no binary POST body API";
    }
    webview::webview child(false, nullptr);
    ASSERT_TRUE(app::binary_rpc::shares_transport_context(main, child));
    app::binary_rpc::authorize_view(child);
    ASSERT_TRUE(app::install_navigation_guard(
        child, "app-rpc://native/index.html"));

    std::string result = "timeout";
    child.bind("reportChild", [&main, &result](const std::string &value) {
        result = value;
        main.terminate();
        return std::string("null");
    });
    const auto timeout = g_timeout_add_seconds(
        10,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &main);
    const std::string html =
        "<!doctype html><script>fetch('app-rpc://native/" +
        std::to_string(app::binary_rpc::method_id("secondOnly")) +
        "',{method:'POST',body:new Uint8Array()})"
        ".then(async r=>{if(!r.ok)throw Error(r.status);"
        "const b=await r.arrayBuffer();reportChild("
        "new DataView(b).getUint8(0)===0&&"
        "new DataView(b).getInt32(1,true)===42?'ok':'bad value')})"
        ".catch(e=>reportChild(String(e)));</script>";
    app::binary_rpc::load_html_with_binary_origin(child, html);
    main.run();
    if (result != "timeout") {
        g_source_remove(timeout);
    }
    EXPECT_EQ(result, R"(["ok"])");
    app::binary_rpc::clear_transport(main);
}

TEST(BinaryWebview, NativeEventUsesBinaryScheme) {
    webview::webview window(false, nullptr);
    if (!app::binary_rpc::install_transport(window, {})) {
        GTEST_SKIP() << "WebKitGTK before 2.40 has no binary scheme support";
    }
    ASSERT_TRUE(app::install_navigation_guard(
        window, "app-rpc://native/index.html"));
    std::string result = "timeout";
    window.bind("ready", [&window, &result](const std::string &) {
        app::binary_rpc::Bytes event{0x82, 0x01, 0x02};
        if (!app::binary_rpc::post_event_bytes(window, event)) {
            result = "queue failed";
            window.terminate();
        }
        return std::string("null");
    });
    window.bind("reportEvent", [&window, &result](const std::string &value) {
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
      window.__APP_NATIVE_EVENT__ = async token => {
        try {
          const url = `app-rpc://native/event/${token}`;
          const response = await fetch(url, {cache: 'no-store'});
          const bytes = new Uint8Array(await response.arrayBuffer());
          const consumed = await fetch(url, {cache: 'no-store'});
          reportEvent(response.ok && bytes.length === 3 &&
            bytes[0] === 0x82 && bytes[1] === 1 && bytes[2] === 2 &&
            consumed.status === 404 ? 'ok' : 'wrong event');
        } catch (error) { reportEvent(String(error)); }
      };
      ready();
    </script>)html";
    app::binary_rpc::load_html_with_binary_origin(window, html);
    window.run();
    if (result != "timeout") {
        g_source_remove(timeout);
    }
    EXPECT_EQ(result, R"(["ok"])");
    app::binary_rpc::clear_transport(window);
}

TEST(NavigationGuard, BlocksForeignTopLevelNavigation) {
    webview::webview window(false, nullptr);
    ASSERT_TRUE(app::install_navigation_guard(
        window, "app-rpc://native/index.html"));
    std::string result = "timeout";
    window.bind("reportNavigation", [&window, &result](const std::string &value) {
        result = value;
        window.terminate();
        return std::string("null");
    });
    const auto timeout = g_timeout_add_seconds(
        5,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &window);
    app::binary_rpc::load_html_with_binary_origin(
        window,
        "<!doctype html><title>trusted</title><script>"
        "location.href='https://example.invalid/foreign';"
        "setTimeout(()=>reportNavigation(document.title),250);"
        "</script>");
    window.run();
    if (result != "timeout") {
        g_source_remove(timeout);
    }
    EXPECT_EQ(result, R"(["trusted"])");
}

TEST(BinaryWebview, RejectsRequestFromUnprivilegedView) {
    webview::webview main(false, nullptr);
    app::binary_rpc::Dispatcher dispatcher;
    dispatcher.bind(7, [](auto &, auto &out) { out.i32(42); });
    if (!app::binary_rpc::install_transport(main, std::move(dispatcher))) {
        GTEST_SKIP() << "WebKitGTK before 2.40 has no binary scheme support";
    }
    webview::webview untrusted(false, nullptr);
    ASSERT_TRUE(app::binary_rpc::shares_transport_context(main, untrusted));
    std::string result = "timeout";
    untrusted.bind("reportDenied", [&main, &result](const std::string &value) {
        result = value;
        main.terminate();
        return std::string("null");
    });
    const auto timeout = g_timeout_add_seconds(
        5,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &main);
    app::binary_rpc::load_html_with_binary_origin(
        untrusted,
        "<!doctype html><script>"
        "fetch('app-rpc://native/7',{method:'POST',body:new Uint8Array()})"
        ".then(r=>reportDenied(String(r.status)))"
        ".catch(e=>reportDenied(String(e)));"
        "</script>");
    main.run();
    if (result != "timeout") {
        g_source_remove(timeout);
    }
    EXPECT_EQ(result, R"(["404"])");
    app::binary_rpc::clear_transport(main);
}

TEST(BinaryWindowManager, CreatesChildWithTypedBootstrap) {
    webview::webview main(false, nullptr);
    app::WindowManager manager(main, false, "", "", {640, 480}, "Main");
    app::WindowBootstrap bootstrap;
    bootstrap.title = "Inspector";
    bootstrap.width = 840;
    bootstrap.extras = app::bindings::JsConv<app::OpaqueValue>::from_json(
        {{"kind", "dockview"}, {"panel", {{"component", "InspectorPanel"}}}});
    const auto id = manager.create_window(std::move(bootstrap));

    struct WaitState {
        app::WindowManager &manager;
        webview::webview &main;
    } state{manager, main};
    const auto poll = g_timeout_add(
        20,
        +[](gpointer data) -> gboolean {
            auto &wait = *static_cast<WaitState *>(data);
            if (wait.manager.list_windows().size() > 1) {
                wait.main.terminate();
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        },
        &state);
    const auto timeout = g_timeout_add_seconds(
        5,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &main);
    main.run();
    const auto windows = manager.list_windows();
    if (windows.size() > 1) {
        g_source_remove(timeout);
    } else {
        g_source_remove(poll);
    }
    ASSERT_EQ(windows.size(), 2U);
    EXPECT_EQ(windows[1].id, id);
    EXPECT_EQ(windows[1].title, "Inspector");
    const auto stored = manager.take_bootstrap(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->window_id, id);
    EXPECT_EQ(stored->width, 840.0);
    EXPECT_EQ(app::bindings::JsConv<app::OpaqueValue>::to_json(stored->extras),
              (app::bindings::json{{"kind", "dockview"},
                                   {"panel", {{"component", "InspectorPanel"}}}}));
}

TEST(BinaryWindowManager, ExternalProductionChildHasNoNativeBindings) {
    webview::webview main(false, nullptr);
    app::WindowManager manager(main, false, "", "about:blank", {640, 480},
                               "Main");
    bool installed = false;
    manager.set_bindings_setup(
        [&installed](webview::webview &) { installed = true; });
    manager.create_window({});

    struct WaitState {
        app::WindowManager &manager;
        webview::webview &main;
    } state{manager, main};
    const auto poll = g_timeout_add(
        20,
        +[](gpointer data) -> gboolean {
            auto &wait = *static_cast<WaitState *>(data);
            if (wait.manager.list_windows().size() > 1) {
                wait.main.terminate();
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        },
        &state);
    const auto timeout = g_timeout_add_seconds(
        5,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &main);
    main.run();
    const auto created = manager.list_windows().size() > 1;
    if (created) {
        g_source_remove(timeout);
    } else {
        g_source_remove(poll);
    }
    ASSERT_TRUE(created);
    EXPECT_FALSE(installed);
}

TEST(BinaryWindowManager, ExternalDevelopmentChildHasNoNativeBindings) {
    webview::webview main(false, nullptr);
    app::WindowManager manager(main, true, "http://127.0.0.1:5173", "",
                               {640, 480}, "Main");
    bool installed = false;
    manager.set_bindings_setup(
        [&installed](webview::webview &) { installed = true; });
    app::WindowBootstrap bootstrap;
    bootstrap.url = "about:blank";
    manager.create_window(std::move(bootstrap));
    struct WaitState {
        app::WindowManager &manager;
        webview::webview &main;
    } state{manager, main};
    const auto poll = g_timeout_add(
        20,
        +[](gpointer data) -> gboolean {
            auto &wait = *static_cast<WaitState *>(data);
            if (wait.manager.list_windows().size() > 1) {
                wait.main.terminate();
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        },
        &state);
    const auto timeout = g_timeout_add_seconds(
        5,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &main);
    main.run();
    const bool created = manager.list_windows().size() > 1;
    if (created) {
        g_source_remove(timeout);
    } else {
        g_source_remove(poll);
    }
    ASSERT_TRUE(created);
    EXPECT_FALSE(installed);
}
