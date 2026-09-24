#pragma once
// =============================================================================
// WindowManager - Gerencia janelas nativas adicionais (multi-janela)
// =============================================================================

#include "app/binary_rpc_transport.h"
#include "app/binding_policy.h"
#include "app/bindings_with_meta.h"
#include "app/drag_tracker.h"
#include "app/native_event.h"
#include "app/native_types.h"
#include "app/navigation_guard.h"
#include "app/navigation_policy.h"
#include "app/window_platform.h"
#include "webview/webview.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if !defined(APP_DEV_MODE) && !defined(APP_NO_EMBEDDED_UI)
#include "embedded_resources.h"
#endif

namespace app {

class WindowManager {
  public:
    using BindingsSetup = std::function<void(webview::webview &)>;
    struct WindowSize {
        int width;
        int height;
    };

    WindowManager(webview::webview &main_window, bool dev_mode,
                  std::string dev_url, std::string custom_url,
                  WindowSize default_size, std::string title_base)
        : main_window_(main_window), dev_mode_(dev_mode),
          dev_url_(std::move(dev_url)), custom_url_(std::move(custom_url)),
          default_width_(default_size.width),
          default_height_(default_size.height),
          title_base_(std::move(title_base)), main_title_(title_base_),
          drag_tracker_(
              main_window_, [this]() { return collect_drag_windows(); },
              [this](const std::string &hovered_id) {
                  on_drag_hover_change(hovered_id);
              }) {}

    ~WindowManager() { stop_drag_tracking(); }

    WindowManager(const WindowManager &) = delete;
    WindowManager &operator=(const WindowManager &) = delete;
    WindowManager(WindowManager &&) = delete;
    WindowManager &operator=(WindowManager &&) = delete;

    void set_bindings_setup(BindingsSetup setup) {
        bindings_setup_ = std::move(setup);
    }

    void set_binary_transport_enabled(bool enabled) {
        binary_transport_enabled_ = enabled;
    }

    std::string create_window(WindowBootstrap bootstrap) {
        std::string window_id = bootstrap.window_id.value_or("");
        if (window_id.empty()) {
            window_id = next_id();
        }
        bootstrap.window_id = window_id;

        {
            std::lock_guard<std::mutex> lock(mu_);
            bootstraps_[window_id] = std::move(bootstrap);
        }

        // Always schedule creation on the UI thread that owns the main loop.
        main_window_.dispatch(
            [this, window_id] { create_window_on_ui_thread(window_id); });
        return window_id;
    }

    std::optional<WindowBootstrap>
    take_bootstrap(const std::string &window_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = bootstraps_.find(window_id);
        if (it == bootstraps_.end()) {
            return std::nullopt;
        }
        WindowBootstrap payload = std::move(it->second);
        bootstraps_.erase(it);
        return payload;
    }

    std::vector<NativeWindowInfo> list_windows() {
        std::vector<NativeWindowInfo> out;
        out.push_back({main_window_id_, main_title_});
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &entry : window_info_) {
            out.push_back({entry.first, entry.second.title});
        }
        return out;
    }

    bool post_opaque_event(const std::string &window_id,
                           const OpaqueValue &event) {
        return post_event(window_id, NativeEvent::forwarded(event));
    }

    bool post_event(const std::string &window_id, NativeEvent event) {
        if (window_id == main_window_id_) {
            main_window_.dispatch([this, payload = std::move(event)] {
                deliver_event(main_window_, std::move(payload),
                              binary_transport_enabled_);
            });
            return true;
        }

        bool exists = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            exists = windows_.find(window_id) != windows_.end();
        }
        if (!exists) {
            return false;
        }
        main_window_.dispatch([this, window_id, payload = std::move(event)] {
            webview::webview *target = nullptr;
            bool binary_ready = false;
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = windows_.find(window_id);
                if (it != windows_.end()) {
                    target = it->second.get();
                    auto info = window_info_.find(window_id);
                    binary_ready =
                        info != window_info_.end() && info->second.binary_ready;
                }
            }
            if (!target) {
                return;
            }
            deliver_event(*target, std::move(payload), binary_ready);
        });
        return true;
    }

    static void deliver_event(webview::webview &window, NativeEvent event,
                              bool binary_ready) {
        try {
            if (binary_ready) {
                auto encoded = encode_native_event(event);
                if (!binary_rpc::post_event_bytes(window, encoded)) {
                    std::cerr << "[WindowManager] Binary event queue rejected "
                                 "event\n";
                }
                return;
            }
            const std::string payload = legacy_native_event(event).dump();
            window.eval("window.dispatchEvent(new CustomEvent('native-event', "
                        "{ detail: " +
                        payload + " }));");
        } catch (const std::exception &error) {
            std::cerr << "[WindowManager] Failed to deliver event: "
                      << error.what() << std::endl;
        }
    }

    bool close_window(const std::string &window_id) {
        bool exists = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            exists = windows_.find(window_id) != windows_.end();
        }
        if (!exists) {
            return false;
        }

        main_window_.dispatch([this, window_id] {
            std::unique_ptr<webview::webview> window;
            bool removed = false;
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = windows_.find(window_id);
                if (it == windows_.end()) {
                    return;
                }
                window = std::move(it->second);
                windows_.erase(it);
                window_info_.erase(window_id);
                removed = true;
            }
            if (removed) {
                emit_main_event(NativeEvent::window_closed(window_id));
            }
        });
        return true;
    }

    void start_drag_tracking(const std::string &origin_window_id,
                             OpaqueValue drag_payload) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            drag_payload_ = std::move(drag_payload);
            drag_origin_id_ = origin_window_id;
            drag_hovered_id_ = origin_window_id;
        }
        drag_tracker_.start(origin_window_id);
    }

    std::optional<OpaqueValue>
    complete_drag_tracking(const std::string &target_window_id) {
        std::optional<OpaqueValue> payload;
        std::string origin_id;
        std::string hovered_id;
        {
            std::lock_guard<std::mutex> lock(mu_);
            payload = std::move(drag_payload_);
            origin_id = drag_origin_id_;
            hovered_id = drag_hovered_id_;
            drag_payload_.reset();
            drag_origin_id_.clear();
            drag_hovered_id_.clear();
        }

        drag_tracker_.stop();

        if (!hovered_id.empty() && hovered_id != origin_id) {
            post_event(hovered_id, NativeEvent::drag_leave(origin_id));
        }

        if (!origin_id.empty() && payload) {
            post_event(origin_id, NativeEvent::drag_complete(
                                      origin_id, target_window_id, *payload));
        }

        return payload;
    }

    std::optional<OutsideDrop>
    complete_drag_outside(const std::string &origin_window_id) {
        const std::string hovered_now =
            drag_tracker_.current_hovered_id(collect_drag_windows());
        const auto cursor = drag_tracker_.current_cursor_position();
        std::optional<OpaqueValue> payload;
        bool should_stop = false;
        std::string origin_id;
        std::string previous_hovered;
        {
            std::lock_guard<std::mutex> lock(mu_);
            origin_id = drag_origin_id_;
            previous_hovered = drag_hovered_id_;
            if (origin_id != origin_window_id) {
                return std::nullopt;
            }
            if (!hovered_now.empty()) {
                return std::nullopt;
            }
            if (!drag_payload_) {
                return std::nullopt;
            }
            payload = std::move(drag_payload_);
            drag_payload_.reset();
            drag_origin_id_.clear();
            drag_hovered_id_.clear();
            should_stop = true;
        }
        if (should_stop) {
            drag_tracker_.stop();
        }
        if (!previous_hovered.empty() && previous_hovered != origin_id) {
            post_event(previous_hovered, NativeEvent::drag_leave(origin_id));
        }
        OutsideDrop result{std::move(*payload), std::nullopt};
        if (cursor) {
            result.drop = DropPoint{static_cast<double>(cursor->x),
                                    static_cast<double>(cursor->y)};
        }
        return result;
    }

    void stop_drag_tracking() {
        std::string origin_id;
        std::string hovered_id;
        {
            std::lock_guard<std::mutex> lock(mu_);
            origin_id = drag_origin_id_;
            hovered_id = drag_hovered_id_;
            drag_payload_.reset();
            drag_origin_id_.clear();
            drag_hovered_id_.clear();
        }

        drag_tracker_.stop();

        if (!hovered_id.empty() && hovered_id != origin_id) {
            post_event(hovered_id, NativeEvent::drag_leave(origin_id));
        }
    }

  private:
    struct WindowInfo {
        std::string title;
        bool binary_ready = false;
    };

    struct WindowConfig {
        int width = 0;
        int height = 0;
        std::string title;
        std::optional<int> left;
        std::optional<int> top;
    };

    static std::string append_window_id(std::string_view url,
                                        const std::string &window_id) {
        const auto hash_pos = url.find('#');
        const std::string base = hash_pos == std::string_view::npos
                                     ? std::string(url)
                                     : std::string(url.substr(0, hash_pos));
        const std::string suffix = hash_pos == std::string_view::npos
                                       ? ""
                                       : std::string(url.substr(hash_pos));
        const char sep = base.find('?') == std::string::npos ? '?' : '&';
        return base + sep + "wid=" + window_id + suffix;
    }

    WindowConfig resolve_window_config(const WindowBootstrap &bootstrap,
                                       const std::string &window_id) const {
        WindowConfig cfg;
        cfg.width = default_width_;
        cfg.height = default_height_;
        cfg.title = title_base_ + " - " + window_id;
        if (bootstrap.title)
            cfg.title = *bootstrap.title;
        cfg.width = rounded_value(bootstrap.width).value_or(default_width_);
        cfg.height = rounded_value(bootstrap.height).value_or(default_height_);
        cfg.left = rounded_value(bootstrap.left);
        cfg.top = rounded_value(bootstrap.top);

        if (cfg.width <= 0) {
            cfg.width = default_width_;
        }
        if (cfg.height <= 0) {
            cfg.height = default_height_;
        }

        return cfg;
    }

    static std::optional<int> rounded_value(std::optional<double> value) {
        if (!value || !std::isfinite(*value) ||
            *value < (std::numeric_limits<int>::min)() ||
            *value > (std::numeric_limits<int>::max)()) {
            return std::nullopt;
        }
        return static_cast<int>(std::lround(*value));
    }

    void apply_window_position(webview::webview &window,
                               const WindowConfig &cfg) const {
        if (!cfg.left || !cfg.top) {
            return;
        }
        auto handle_result = window.window();
        if (!handle_result.ok()) {
            return;
        }
        move_window_to(handle_result.value(), *cfg.left, *cfg.top);
    }

    std::string resolve_url(const WindowBootstrap &bootstrap,
                            const std::string &window_id) const {
        std::string base = bootstrap.url.value_or("");

        if (base.empty()) {
            base = !custom_url_.empty() ? custom_url_
                                        : (dev_mode_ ? dev_url_ : "");
        }
        if (base.empty()) {
            return {};
        }
        if (base.find("wid=") != std::string::npos) {
            return base;
        }
        return append_window_id(base, window_id);
    }

    void load_content(webview::webview &window, const std::string &window_id,
                      const WindowBootstrap &bootstrap,
                      [[maybe_unused]] bool binary_ready) const {
        const std::string id_literal = bindings::json(window_id).dump();
        const std::string init_script =
            "window.__APP_WINDOW_ID__ = " + id_literal + ";";
        window.init(init_script);

        const std::string url = resolve_url(bootstrap, window_id);
        if (!url.empty()) {
            window.navigate(url);
            return;
        }

#if defined(APP_DEV_MODE)
        throw std::runtime_error("Dev build without Vite server URL");
#elif defined(APP_NO_EMBEDDED_UI)
        binary_rpc::load_html_with_binary_origin(
            window, "<!doctype html><html><body></body></html>");
#else
        if (binary_ready) {
            binary_rpc::load_html_with_binary_origin(window, INDEX_HTML);
        } else {
            window.set_html(INDEX_HTML);
        }
#endif
    }

    void emit_main_event(NativeEvent event) {
        post_event(main_window_id_, std::move(event));
    }

    void handle_window_creation_failure(const std::string &window_id,
                                        const std::string &message) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            bootstraps_.erase(window_id);
            windows_.erase(window_id);
            window_info_.erase(window_id);
        }
        std::cerr << "[WindowManager] Failed to create window '" << window_id
                  << "': " << message << std::endl;
        emit_main_event(NativeEvent::window_error(window_id, message));
    }

    void create_window_on_ui_thread(const std::string &window_id) {
        WindowBootstrap bootstrap_snapshot;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = bootstraps_.find(window_id);
            if (it != bootstraps_.end()) {
                bootstrap_snapshot = it->second;
            }
        }

        const WindowConfig cfg =
            resolve_window_config(bootstrap_snapshot, window_id);

        try {
            auto window = binary_rpc::create_webview(dev_mode_, nullptr);
            window->set_title(cfg.title);
            window->set_size(cfg.width, cfg.height, WEBVIEW_HINT_NONE);
            apply_window_position(*window, cfg);
            auto parent_handle = main_window_.window();
            auto child_handle = window->window();
            if (parent_handle.ok() && child_handle.ok()) {
                attach_window_to_parent(parent_handle.value(),
                                        child_handle.value());
            }

            const auto content_url = resolve_url(bootstrap_snapshot, window_id);
            bool trusted = false;
            if (should_install_bindings(custom_url_)) {
                if (dev_mode_) {
                    const auto origin = parse_trusted_origin(dev_url_);
                    trusted =
                        origin && is_trusted_navigation(content_url, *origin);
                } else {
                    trusted = content_url.empty();
                }
            }
            std::string_view trusted_navigation_url = dev_url_;
            if (!dev_mode_) {
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
                trusted_navigation_url = binary_rpc::rpc_base;
#else
                trusted_navigation_url = {};
#endif
            }
            if (trusted &&
                !install_navigation_guard(*window, trusted_navigation_url)) {
                throw std::runtime_error("Failed to guard child navigation");
            }
            const bool binary_ready =
                trusted && binary_transport_enabled_ && content_url.empty() &&
                binary_rpc::shares_transport_context(main_window_, *window);
            if (trusted && binary_transport_enabled_ && !binary_ready) {
                throw std::runtime_error(
                    "Child cannot share binary RPC transport");
            }
            if (binary_ready) {
                binary_rpc::authorize_view(*window);
                window->init("window.__APP_BINARY_RPC__ = { endpoint: '" +
                             std::string(binary_rpc::rpc_base) + "' };");
            } else if (bindings_setup_ && trusted) {
                bindings_setup_(*window);
            }
            load_content(*window, window_id, bootstrap_snapshot, binary_ready);

            {
                std::lock_guard<std::mutex> lock(mu_);
                windows_[window_id] = std::move(window);
                window_info_[window_id] = WindowInfo{cfg.title, binary_ready};
            }
        } catch (const std::exception &e) {
            handle_window_creation_failure(window_id, e.what());
        } catch (...) {
            handle_window_creation_failure(window_id, "Unknown error");
        }
    }

    std::string next_id() {
        const unsigned int value = next_id_.fetch_add(1);
        return "w" + std::to_string(value);
    }

    std::vector<DragWindow> collect_drag_windows() {
        std::vector<DragWindow> windows;
        auto main_handle = main_window_.window();
        if (main_handle.ok()) {
            windows.push_back({main_window_id_, main_handle.value()});
        }

        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &entry : windows_) {
            if (!entry.second) {
                continue;
            }
            auto handle = entry.second->window();
            if (handle.ok()) {
                windows.push_back({entry.first, handle.value()});
            }
        }
        return windows;
    }

    void on_drag_hover_change(const std::string &hovered_id) {
        std::string origin_id;
        std::string previous_id;
        bool has_payload = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            origin_id = drag_origin_id_;
            previous_id = drag_hovered_id_;
            has_payload = drag_payload_.has_value();
            if (hovered_id == previous_id) {
                return;
            }
            drag_hovered_id_ = hovered_id;
        }

        if (!previous_id.empty() && previous_id != origin_id) {
            post_event(previous_id, NativeEvent::drag_leave(origin_id));
        }
        if (!hovered_id.empty() && hovered_id != origin_id && has_payload) {
            post_event(hovered_id, NativeEvent::drag_hover(origin_id));
        }
    }

    webview::webview &main_window_;
    bool dev_mode_ = false;
    std::string dev_url_;
    std::string custom_url_;
    int default_width_ = 0;
    int default_height_ = 0;
    std::string title_base_;
    std::string main_window_id_ = "main";
    std::string main_title_;
    bool binary_transport_enabled_ = false;
    std::atomic_uint next_id_{1};

    std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<webview::webview>> windows_;
    std::unordered_map<std::string, WindowInfo> window_info_;
    std::unordered_map<std::string, WindowBootstrap> bootstraps_;
    std::optional<OpaqueValue> drag_payload_;
    std::string drag_origin_id_;
    std::string drag_hovered_id_;
    DragTracker drag_tracker_;
    BindingsSetup bindings_setup_;
};

} // namespace app
