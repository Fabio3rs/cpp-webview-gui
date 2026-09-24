#include <gtest/gtest.h>

#include "app/binary_rpc_bindings.h"
#include "app/binary_rpc_transport.h"
#include "app/dev_rpc_server.h"
#include "app/native_event.h"
#include "dev_server.h"

#include <chrono>
#include <string>
#include <thread>

TEST(DevBinaryRpc, ViteProxyTransfersTypedBytes) {
    dev::ServerConfig config = dev::get_default_config();
    config.working_dir = std::string(APP_SOURCE_DIR) + "/ui";
    dev::ServerProcess vite;
    ASSERT_TRUE(dev::ensure_server_running(config, vite));
    struct ViteCleanup {
        dev::ServerProcess &process;
        ~ViteCleanup() {
            if (process.owned)
                dev::stop_server(process);
        }
    } vite_cleanup{vite};

    auto other_project = config;
    other_project.working_dir += "/other-project";
    dev::ServerProcess foreign_vite;
    EXPECT_FALSE(dev::ensure_server_running(other_project, foreign_vite));
    EXPECT_FALSE(foreign_vite.owned);

    auto window = app::binary_rpc::create_webview(false, nullptr);
    ASSERT_TRUE(window);
    app::binary_rpc::Dispatcher dispatcher;
    dispatcher.bind(7, [](auto &, auto &out) { out.i32(42); });
    dispatcher.bind(8, [](auto &in, auto &out) { out.bytes(in.bytes()); });
    dispatcher.bind(app::binary_rpc::method_id("getCounter"),
                    [](auto &, auto &out) { out.i32(42); });
    ASSERT_TRUE(app::binary_rpc::install_transport(*window, {}));
    const auto ports = app::dev_ports::get();
    app::DevRpcServer server(*window, std::move(dispatcher), ports.rpc,
                             app::dev_ports::vite_origin(ports));
    ASSERT_TRUE(server.start());

    std::string result = "timeout";
    window->bind("reportDevTest", [&result, &window](const std::string &value) {
        result = value;
        window->terminate();
        return std::string("null");
    });
    window->bind("readyDevEvent", [&result, &window](const std::string &) {
        auto event = app::encode_native_event(
            app::NativeEvent::window_closed("dev-window"));
        if (!app::binary_rpc::post_event_bytes(*window, event)) {
            result = "event queue failed";
            window->terminate();
        }
        return std::string("null");
    });
    window->init(
        "window.__APP_BINARY_RPC__ = { endpoint: '/__native_rpc/', token: '" +
        server.token() + "' };" + R"js(
      window.addEventListener('load', async () => {
        try {
          const base = '/__native_rpc/';
          const headers = { 'X-App-Rpc-Token': window.__APP_BINARY_RPC__.token };
          const denied = await fetch(base + '7', {
            method: 'POST', body: new Uint8Array()
          });
          if (denied.status !== 403) throw new Error('missing-token request accepted');
          const count = await fetch(base + '7', {
            method: 'POST', body: new Uint8Array(), headers
          });
          const value = new DataView(await count.arrayBuffer());
          if (!count.ok || value.byteLength !== 5 || value.getUint8(0) !== 0 ||
              value.getInt32(1, true) !== 42) throw new Error('typed response failed');
          const size = 15 * 1024 * 1024 - 4;
          const bytes = new Uint8Array(size + 4);
          new DataView(bytes.buffer).setUint32(0, size, true);
          bytes.fill(0xab, 4);
          const echo = await fetch(base + '8', {
            method: 'POST', body: bytes, headers
          });
          const returned = new Uint8Array(await echo.arrayBuffer());
          if (!echo.ok || returned.length !== size + 5 || returned[0] !== 0 ||
              !returned.subarray(5).every(byte => byte === 0xab))
            throw new Error('bulk bytes failed');
          for (let attempt = 0; attempt < 100 && !window.getCounter; ++attempt)
            await new Promise(resolve => setTimeout(resolve, 25));
          const binding = await window.getCounter?.();
          if (!binding?.ok || binding.data !== 42)
            throw new Error('named binary binding failed');
          await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('event timeout')), 5000);
            const onEvent = event => {
              if (event.detail?.type !== 'native-window.closed' ||
                  event.detail.windowId !== 'dev-window') return;
              clearTimeout(timer);
              window.removeEventListener('native-event', onEvent);
              resolve();
            };
            window.addEventListener('native-event', onEvent);
            readyDevEvent().catch(reject);
          });
          reportDevTest('ok');
        } catch (error) { reportDevTest(String(error)); }
      }, { once: true });
    )js");

    std::jthread watchdog([&window](std::stop_token stop) {
        for (int tick = 0; tick < 600 && !stop.stop_requested(); ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!stop.stop_requested()) {
            window->dispatch([&window] { window->terminate(); });
        }
    });
    window->navigate(config.dev_url);
    window->run();
    watchdog.request_stop();
    EXPECT_EQ(result, R"(["ok"])");
    app::binary_rpc::clear_transport(*window);
}
