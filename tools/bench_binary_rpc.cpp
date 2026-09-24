#include "app/binary_rpc_bindings.h"
#include "app/binary_rpc_transport.h"

#include <glib.h>

#include <iostream>
#include <string>

int main() {
    app::binary_rpc::Dispatcher dispatcher;
    dispatcher.bind(1, [](auto &, auto &out) { out.i32(42); });
    dispatcher.bind(2, [](auto &in, auto &out) { out.bytes(in.bytes()); });

    webview::webview window(false, nullptr);
    if (!app::binary_rpc::install_transport(window, std::move(dispatcher))) {
        std::cerr << "WebKitGTK 2.40+ is required\n";
        return 1;
    }
    window.bind("legacyCounter",
                [](const std::string &) { return std::string("42"); });
    // Favor the legacy bridge: the native side echoes base64 without decoding.
    window.bind("legacyEcho", [](const std::string &args) { return args; });

    bool finished = false;
    window.bind("report", [&window, &finished](const std::string &result) {
        std::cout << result << '\n';
        finished = true;
        window.terminate();
        return std::string("null");
    });
    const auto timeout = g_timeout_add_seconds(
        45,
        +[](gpointer data) -> gboolean {
            static_cast<webview::webview *>(data)->terminate();
            return G_SOURCE_REMOVE;
        },
        &window);

    app::binary_rpc::load_html_with_binary_origin(window,
                                                  R"html(<!doctype html><script>
      async function measure(count, call) {
        const start = performance.now();
        for (let i = 0; i < count; ++i) await call();
        return (performance.now() - start) / count;
      }
      (async () => {
        const small = new Uint8Array(0);
        const binarySmall = async () => {
          const response = await fetch('app-rpc://native/1', {
            method: 'POST', body: small,
            headers: {'Content-Type': 'application/octet-stream'}
          });
          const data = new DataView(await response.arrayBuffer());
          if (data.getUint8(0) !== 0 || data.getInt32(1, true) !== 42)
            throw new Error('binary counter');
        };
        const jsonSmall = async () => {
          if (await legacyCounter() !== 42) throw new Error('JSON counter');
        };
        await measure(20, jsonSmall);
        await measure(20, binarySmall);
        const smallJsonMs = await measure(300, jsonSmall);
        const smallBinaryMs = await measure(300, binarySmall);

        const size = 1024 * 1024;
        const payload = new Uint8Array(size + 4);
        new DataView(payload.buffer).setUint32(0, size, true);
        payload.fill(0xab, 4);
        const binaryBulk = async () => {
          const response = await fetch('app-rpc://native/2', {
            method: 'POST', body: payload,
            headers: {'Content-Type': 'application/octet-stream'}
          });
          const result = new Uint8Array(await response.arrayBuffer());
          if (result[0] !== 0 || result.length !== payload.length + 1 ||
              result[5] !== 0xab) {
            throw new Error('binary bulk');
          }
        };
        let raw = '';
        for (let offset = 4; offset < payload.length; offset += 8192) {
          raw += String.fromCharCode(...payload.subarray(offset, offset + 8192));
        }
        const jsonBulk = async () => {
          const encoded = btoa(raw);
          const result = await legacyEcho(encoded);
          if (atob(result[0]).length !== size) throw new Error('JSON bulk');
        };
        await measure(2, jsonBulk);
        await measure(2, binaryBulk);
        const bulkJsonMs = await measure(5, jsonBulk);
        const bulkBinaryMs = await measure(5, binaryBulk);
        report(`small JSON ${smallJsonMs.toFixed(3)} ms, direct ${smallBinaryMs.toFixed(3)} ms; ` +
          `1 MiB JSON/base64 ${bulkJsonMs.toFixed(3)} ms, binary ${bulkBinaryMs.toFixed(3)} ms`);
      })().catch(error => report(String(error)));
    </script>)html");
    window.run();
    if (finished) {
        g_source_remove(timeout);
    }
    return finished ? 0 : 1;
}
