#pragma once

#include "app/binary_rpc.h"
#include "webview/webview.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace app {

// Loopback-only byte endpoint used by Vite's same-origin development proxy.
class DevRpcServer {
  public:
    DevRpcServer(webview::webview &window, binary_rpc::Dispatcher dispatcher);
    ~DevRpcServer();

    DevRpcServer(const DevRpcServer &) = delete;
    DevRpcServer &operator=(const DevRpcServer &) = delete;

    [[nodiscard]] bool start();
    [[nodiscard]] const std::string &token() const noexcept { return token_; }

  private:
    void serve();
    void handle_client(std::uintptr_t socket);

    webview::webview &window_;
    binary_rpc::Dispatcher dispatcher_;
    std::string token_;
    std::atomic<bool> stopping_{false};
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
    std::uintptr_t listener_ = 0;
    std::thread thread_;
};

} // namespace app
