#pragma once
// =============================================================================
// DragTracker - Polling do cursor para detectar janela sob o mouse
// =============================================================================

#include "webview/webview.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace app {

struct DragWindow {
    std::string id;
    void *handle = nullptr;
};

struct DragCursor {
    int x = 0;
    int y = 0;
};

class DragTracker {
  public:
    using WindowProvider = std::function<std::vector<DragWindow>()>;
    using HoverCallback = std::function<void(const std::string &)>;

    DragTracker(webview::webview &ui_window, WindowProvider window_provider,
                HoverCallback on_hover);
    ~DragTracker();

    DragTracker(const DragTracker &) = delete;
    DragTracker &operator=(const DragTracker &) = delete;
    DragTracker(DragTracker &&) = delete;
    DragTracker &operator=(DragTracker &&) = delete;

    void start(const std::string &origin_window_id);
    void stop();
    bool active() const;
    std::string
    current_hovered_id(const std::vector<DragWindow> &windows) const;
    std::optional<DragCursor> current_cursor_position() const;

  private:
    void ensure_worker();
    void worker_loop();
    void tick_ui();

    webview::webview &ui_window_;
    WindowProvider window_provider_;
    HoverCallback on_hover_;

    std::atomic_bool active_{false};
    std::atomic_bool stop_requested_{false};
    std::atomic_bool tick_scheduled_{false};
    std::mutex mu_;
    std::string last_hovered_id_;
    std::thread worker_;
};

} // namespace app
