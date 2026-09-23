#include <gtest/gtest.h>

#include "app/binary_rpc_transport.h"
#include "app/navigation_guard.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

TEST(BinaryRpcPlatform, TransfersBytesThroughRealWebView) {
    app::binary_rpc::Dispatcher dispatcher;
    dispatcher.bind(7, [](auto &input, auto &output) {
        output.i32(input.i32() + input.i32());
    });
    dispatcher.bind(
        8, [](auto &input, auto &output) { output.bytes(input.bytes()); });

    auto window = app::binary_rpc::create_webview(false, nullptr);
    ASSERT_TRUE(window);
    ASSERT_TRUE(
        app::install_navigation_guard(*window, app::binary_rpc::page_url));
    ASSERT_TRUE(
        app::binary_rpc::install_transport(*window, std::move(dispatcher)));

    std::string result = "timeout";
    window->bind("report", [&result, &window](const std::string &value) {
        result = value;
        window->terminate();
        return std::string("null");
    });

    std::jthread watchdog([&window](std::stop_token stop) {
        for (int tick = 0; tick < 600 && !stop.stop_requested(); ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!stop.stop_requested()) {
            window->dispatch([&window] { window->terminate(); });
        }
    });

    const std::string html = "<!doctype html><script>const rpcBase='" +
                             std::string(app::binary_rpc::rpc_base) + R"html(';
        (async () => {
          const values = new Uint8Array(8);
          const input = new DataView(values.buffer);
          input.setInt32(0, 20, true);
          input.setInt32(4, 22, true);
          const small = await fetch(rpcBase + '7', {
            method: 'POST', body: values,
            headers: {'Content-Type': 'application/octet-stream'}
          });
          if (!small.ok) throw new Error('small HTTP ' + small.status);
          const sum = new DataView(await small.arrayBuffer());
          if (sum.getUint8(0) !== 0 || sum.getInt32(1, true) !== 42)
            throw new Error('wrong sum');

          const count = 15 * 1024 * 1024 - 4;
          const large = new Uint8Array(count + 4);
          new DataView(large.buffer).setUint32(0, count, true);
          large.fill(0xab, 4);
          const echo = await fetch(rpcBase + '8', {
            method: 'POST', body: large,
            headers: {'Content-Type': 'application/octet-stream'}
          });
          if (!echo.ok) throw new Error('echo HTTP ' + echo.status);
          const echoed = new Uint8Array(await echo.arrayBuffer());
          if (echoed[0] !== 0 || echoed.length !== count + 5 ||
              new DataView(echoed.buffer).getUint32(1, true) !== count ||
              !echoed.subarray(5).every(byte => byte === 0xab))
            throw new Error('wrong bytes');

          const unknown = await fetch(rpcBase + '4294967295', {
            method: 'POST', body: new Uint8Array()
          });
          if (!unknown.ok ||
              new Uint8Array(await unknown.arrayBuffer())[0] !== 1)
            throw new Error('missing error envelope');
          report('ok');
        })().catch(error => report(String(error)));
        </script>)html";

    app::binary_rpc::load_html_with_binary_origin(*window, html);
    window->run();
    watchdog.request_stop();
    EXPECT_EQ(result, R"(["ok"])");
    app::binary_rpc::clear_transport(*window);
}
