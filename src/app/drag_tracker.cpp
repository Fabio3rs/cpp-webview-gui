#include "app/drag_tracker.h"
#include <chrono>
#include <optional>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/objc-runtime.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

namespace app {
namespace {

struct ScreenPoint {
    int x = 0;
    int y = 0;
};

struct ScreenRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

bool contains(const ScreenRect &rect, const ScreenPoint &point) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

std::optional<ScreenPoint> get_cursor_position() {
#if defined(_WIN32)
    POINT pt{};
    if (!GetCursorPos(&pt)) {
        return std::nullopt;
    }
    return ScreenPoint{pt.x, pt.y};
#elif defined(__APPLE__)
    struct NSPoint {
        double x;
        double y;
    };

    Class ns_event = objc_getClass("NSEvent");
    if (!ns_event) {
        return std::nullopt;
    }
    auto point = reinterpret_cast<NSPoint (*)(id, SEL)>(objc_msgSend)(
        reinterpret_cast<id>(ns_event), sel_registerName("mouseLocation"));
    return ScreenPoint{static_cast<int>(point.x), static_cast<int>(point.y)};
#elif defined(__linux__)
#if GTK_MAJOR_VERSION >= 4
    // TODO: Implementar via GDK4 (gdk_surface_get_device_position).
    return std::nullopt;
#else
    auto *display = gdk_display_get_default();
    if (!display) {
        return std::nullopt;
    }
    auto *seat = gdk_display_get_default_seat(display);
    if (!seat) {
        return std::nullopt;
    }
    auto *pointer = gdk_seat_get_pointer(seat);
    if (!pointer) {
        return std::nullopt;
    }
    int x = 0;
    int y = 0;
    gdk_device_get_position(pointer, nullptr, &x, &y);
    return ScreenPoint{x, y};
#endif
#else
    return std::nullopt;
#endif
}

std::optional<ScreenRect> get_window_bounds(void *handle) {
    if (!handle) {
        return std::nullopt;
    }

#if defined(_WIN32)
    RECT rect{};
    if (!GetWindowRect(static_cast<HWND>(handle), &rect)) {
        return std::nullopt;
    }
    return ScreenRect{rect.left, rect.top, rect.right - rect.left,
                      rect.bottom - rect.top};
#elif defined(__APPLE__)
    struct NSPoint {
        double x;
        double y;
    };
    struct NSSize {
        double width;
        double height;
    };
    struct NSRect {
        NSPoint origin;
        NSSize size;
    };

    auto frame = reinterpret_cast<NSRect (*)(id, SEL)>(objc_msgSend)(
        static_cast<id>(handle), sel_registerName("frame"));
    return ScreenRect{static_cast<int>(frame.origin.x),
                      static_cast<int>(frame.origin.y),
                      static_cast<int>(frame.size.width),
                      static_cast<int>(frame.size.height)};
#elif defined(__linux__)
#if GTK_MAJOR_VERSION >= 4
    // TODO: Implementar via GDK4 (gtk_native_get_surface +
    // gdk_surface_get_position).
    return std::nullopt;
#else
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    gtk_window_get_position(GTK_WINDOW(handle), &x, &y);
    gtk_window_get_size(GTK_WINDOW(handle), &width, &height);
    return ScreenRect{x, y, width, height};
#endif
#else
    return std::nullopt;
#endif
}

std::string find_window_under_cursor(const std::vector<DragWindow> &windows) {
    auto cursor = get_cursor_position();
    if (!cursor) {
        return {};
    }

    for (const auto &window : windows) {
        const auto bounds = get_window_bounds(window.handle);
        if (!bounds) {
            continue;
        }
        if (contains(*bounds, *cursor)) {
            return window.id;
        }
    }
    return {};
}

} // namespace

DragTracker::DragTracker(webview::webview &ui_window,
                         WindowProvider window_provider, HoverCallback on_hover)
    : ui_window_(ui_window), window_provider_(std::move(window_provider)),
      on_hover_(std::move(on_hover)) {}

DragTracker::~DragTracker() {
    stop();
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DragTracker::start(const std::string &origin_window_id) {
    (void)origin_window_id;
    {
        std::lock_guard<std::mutex> lock(mu_);
        last_hovered_id_.clear();
    }
    active_.store(true);
    ensure_worker();
}

void DragTracker::stop() {
    active_.store(false);
    tick_scheduled_.store(false);
    std::lock_guard<std::mutex> lock(mu_);
    last_hovered_id_.clear();
}

bool DragTracker::active() const { return active_.load(); }

std::string
DragTracker::current_hovered_id(const std::vector<DragWindow> &windows) const {
    return find_window_under_cursor(windows);
}

std::optional<DragCursor> DragTracker::current_cursor_position() const {
    const auto cursor = get_cursor_position();
    if (!cursor) {
        return std::nullopt;
    }
    return DragCursor{cursor->x, cursor->y};
}

void DragTracker::ensure_worker() {
    if (worker_.joinable()) {
        return;
    }
    stop_requested_.store(false);
    worker_ = std::thread([this]() { worker_loop(); });
}

void DragTracker::worker_loop() {
    while (!stop_requested_.load()) {
        if (active_.load() && !tick_scheduled_.exchange(true)) {
            ui_window_.dispatch([this]() { tick_ui(); });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void DragTracker::tick_ui() {
    tick_scheduled_.store(false);
    if (!active_.load()) {
        return;
    }

    const auto windows =
        window_provider_ ? window_provider_() : std::vector<DragWindow>{};
    const std::string hovered_id = find_window_under_cursor(windows);

    std::string last_hovered;
    {
        std::lock_guard<std::mutex> lock(mu_);
        last_hovered = last_hovered_id_;
    }

    if (hovered_id == last_hovered) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        last_hovered_id_ = hovered_id;
    }

    if (on_hover_) {
        on_hover_(hovered_id);
    }
}

} // namespace app
