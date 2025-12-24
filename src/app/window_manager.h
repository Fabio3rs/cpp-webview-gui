#pragma once
// =============================================================================
// WindowManager - Gerencia janelas nativas adicionais (multi-janela)
// =============================================================================

#include "app/drag_tracker.h"
#include "app/window_platform.h"
#include "webview/webview.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if !defined(APP_DEV_MODE) && !defined(APP_NO_EMBEDDED_UI)
#include "embedded_resources.h"
#endif

namespace app {

class WindowManager {
  public:
    using json = nlohmann::json;
    using BindingsSetup = std::function<void(webview::webview &)>;

    WindowManager(webview::webview &main_window, bool dev_mode,
                  std::string dev_url, std::string custom_url,
                  int default_width, int default_height, std::string title_base)
        : main_window_(main_window), dev_mode_(dev_mode),
          dev_url_(std::move(dev_url)), custom_url_(std::move(custom_url)),
          default_width_(default_width), default_height_(default_height),
          title_base_(std::move(title_base)), main_title_(title_base_),
          drag_tracker_(
              main_window_, [this]() { return collect_drag_windows(); },
              [this](const std::string &hovered_id) {
                  on_drag_hover_change(hovered_id);
              }) {}

    ~WindowManager() { stop_drag_tracking(); }

    void set_bindings_setup(BindingsSetup setup) {
        bindings_setup_ = std::move(setup);
    }

    std::string create_window(json bootstrap) {
        if (!bootstrap.is_object()) {
            bootstrap = json::object();
        }

        std::string window_id;
        auto id_it = bootstrap.find("windowId");
        if (id_it != bootstrap.end() && id_it->is_string()) {
            window_id = id_it->get<std::string>();
        }
        if (window_id.empty()) {
            window_id = next_id();
        }
        bootstrap["windowId"] = window_id;

        {
            std::lock_guard<std::mutex> lock(mu_);
            bootstraps_[window_id] = std::move(bootstrap);
        }

        // Always schedule creation on the UI thread that owns the main loop.
        main_window_.dispatch(
            [this, window_id] { create_window_on_ui_thread(window_id); });
        return window_id;
    }

    std::optional<json> take_bootstrap(const std::string &window_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = bootstraps_.find(window_id);
        if (it == bootstraps_.end()) {
            return std::nullopt;
        }
        json payload = std::move(it->second);
        bootstraps_.erase(it);
        return payload;
    }

    json list_windows() {
        json out = json::array();
        out.push_back({{"id", main_window_id_}, {"title", main_title_}});
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &entry : window_info_) {
            out.push_back({{"id", entry.first}, {"title", entry.second.title}});
        }
        return out;
    }

    bool post_event(const std::string &window_id, const json &event) {
        const std::string payload = event.dump();
        if (window_id == main_window_id_) {
            main_window_.dispatch([this, payload] {
                main_window_.eval("window.dispatchEvent(new "
                                  "CustomEvent('native-event', { detail: " +
                                  payload + " }));");
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
        main_window_.dispatch([this, window_id, payload] {
            webview::webview *target = nullptr;
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = windows_.find(window_id);
                if (it != windows_.end()) {
                    target = it->second.get();
                }
            }
            if (!target) {
                return;
            }
            target->eval("window.dispatchEvent(new CustomEvent('native-event', "
                         "{ detail: " +
                         payload + " }));");
        });
        return true;
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
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = windows_.find(window_id);
                if (it == windows_.end()) {
                    return;
                }
                window = std::move(it->second);
                windows_.erase(it);
                window_info_.erase(window_id);
            }
        });
        return true;
    }

    void start_drag_tracking(const std::string &origin_window_id,
                             json drag_payload) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            drag_payload_ = std::move(drag_payload);
            drag_origin_id_ = origin_window_id;
            drag_hovered_id_ = origin_window_id;
        }
        drag_tracker_.start(origin_window_id);
    }

    json complete_drag_tracking(const std::string &target_window_id) {
        json payload;
        std::string origin_id;
        std::string hovered_id;
        {
            std::lock_guard<std::mutex> lock(mu_);
            payload = drag_payload_;
            origin_id = drag_origin_id_;
            hovered_id = drag_hovered_id_;
            drag_payload_ = json();
            drag_origin_id_.clear();
            drag_hovered_id_.clear();
        }

        drag_tracker_.stop();

        if (!hovered_id.empty() && hovered_id != origin_id) {
            post_event(hovered_id,
                       {{"type", "dock.dragLeave"},
                        {"payload", {{"originWindowId", origin_id}}}});
        }

        if (!origin_id.empty() && !payload.is_null()) {
            post_event(origin_id, {{"type", "dock.dragComplete"},
                                   {"payload",
                                    {{"originWindowId", origin_id},
                                     {"targetWindowId", target_window_id},
                                     {"dragPayload", payload}}}});
        }

        return payload;
    }

    json complete_drag_outside(const std::string &origin_window_id) {
        const std::string hovered_now =
            drag_tracker_.current_hovered_id(collect_drag_windows());
        const auto cursor = drag_tracker_.current_cursor_position();
        json payload;
        bool should_stop = false;
        std::string origin_id;
        std::string previous_hovered;
        {
            std::lock_guard<std::mutex> lock(mu_);
            origin_id = drag_origin_id_;
            previous_hovered = drag_hovered_id_;
            if (origin_id != origin_window_id) {
                return json();
            }
            if (!hovered_now.empty()) {
                return json();
            }
            if (drag_payload_.is_null()) {
                return json();
            }
            payload = drag_payload_;
            drag_payload_ = json();
            drag_origin_id_.clear();
            drag_hovered_id_.clear();
            should_stop = true;
        }
        if (should_stop) {
            drag_tracker_.stop();
        }
        if (!previous_hovered.empty() && previous_hovered != origin_id) {
            post_event(previous_hovered,
                       {{"type", "dock.dragLeave"},
                        {"payload", {{"originWindowId", origin_id}}}});
        }
        json result = json::object();
        result["payload"] = payload;
        if (cursor) {
            result["drop"] = {{"x", cursor->x}, {"y", cursor->y}};
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
            drag_payload_ = json();
            drag_origin_id_.clear();
            drag_hovered_id_.clear();
        }

        drag_tracker_.stop();

        if (!hovered_id.empty() && hovered_id != origin_id) {
            post_event(hovered_id,
                       {{"type", "dock.dragLeave"},
                        {"payload", {{"originWindowId", origin_id}}}});
        }
    }

  private:
    struct WindowInfo {
        std::string title;
    };

    struct WindowConfig {
        int width = 0;
        int height = 0;
        std::string title;
        std::optional<int> left;
        std::optional<int> top;
    };

    static std::string append_window_id(const std::string &url,
                                        const std::string &window_id) {
        const auto hash_pos = url.find('#');
        const std::string base =
            hash_pos == std::string::npos ? url : url.substr(0, hash_pos);
        const std::string suffix =
            hash_pos == std::string::npos ? "" : url.substr(hash_pos);
        const char sep = base.find('?') == std::string::npos ? '?' : '&';
        return base + sep + "wid=" + window_id + suffix;
    }

    WindowConfig resolve_window_config(const json &bootstrap,
                                       const std::string &window_id) const {
        WindowConfig cfg;
        cfg.width = default_width_;
        cfg.height = default_height_;
        cfg.title = title_base_ + " - " + window_id;

        if (bootstrap.is_object()) {
            auto title_it = bootstrap.find("title");
            if (title_it != bootstrap.end() && title_it->is_string()) {
                cfg.title = title_it->get<std::string>();
            }

            auto width_it = bootstrap.find("width");
            if (width_it != bootstrap.end() && width_it->is_number()) {
                cfg.width =
                    static_cast<int>(std::lround(width_it->get<double>()));
            }

            auto height_it = bootstrap.find("height");
            if (height_it != bootstrap.end() && height_it->is_number()) {
                cfg.height =
                    static_cast<int>(std::lround(height_it->get<double>()));
            }

            auto left_it = bootstrap.find("left");
            if (left_it != bootstrap.end() && left_it->is_number()) {
                cfg.left =
                    static_cast<int>(std::lround(left_it->get<double>()));
            }

            auto top_it = bootstrap.find("top");
            if (top_it != bootstrap.end() && top_it->is_number()) {
                cfg.top = static_cast<int>(std::lround(top_it->get<double>()));
            }
        }

        if (cfg.width <= 0) {
            cfg.width = default_width_;
        }
        if (cfg.height <= 0) {
            cfg.height = default_height_;
        }

        return cfg;
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

    std::string resolve_url(const json &bootstrap,
                            const std::string &window_id) const {
        std::string base;
        if (bootstrap.is_object()) {
            auto url_it = bootstrap.find("url");
            if (url_it != bootstrap.end() && url_it->is_string()) {
                base = url_it->get<std::string>();
            }
        }

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
                      const json &bootstrap) const {
        const std::string id_literal = json(window_id).dump();
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
        window.set_html("<!doctype html><html><body></body></html>");
#else
        window.set_html(INDEX_HTML);
#endif
    }

    void create_window_on_ui_thread(const std::string &window_id) {
        json bootstrap_snapshot;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = bootstraps_.find(window_id);
            if (it != bootstraps_.end()) {
                bootstrap_snapshot = it->second;
            }
        }

        const WindowConfig cfg =
            resolve_window_config(bootstrap_snapshot, window_id);
        auto window = std::make_unique<webview::webview>(dev_mode_, nullptr);
        window->set_title(cfg.title);
        window->set_size(cfg.width, cfg.height, WEBVIEW_HINT_NONE);
        apply_window_position(*window, cfg);
        auto parent_handle = main_window_.window();
        auto child_handle = window->window();
        if (parent_handle.ok() && child_handle.ok()) {
            attach_window_to_parent(parent_handle.value(),
                                    child_handle.value());
        }

        if (bindings_setup_) {
            bindings_setup_(*window);
        }
        load_content(*window, window_id, bootstrap_snapshot);

        {
            std::lock_guard<std::mutex> lock(mu_);
            windows_[window_id] = std::move(window);
            window_info_[window_id] = WindowInfo{cfg.title};
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
        json payload;
        {
            std::lock_guard<std::mutex> lock(mu_);
            origin_id = drag_origin_id_;
            previous_id = drag_hovered_id_;
            payload = drag_payload_;
            if (hovered_id == previous_id) {
                return;
            }
            drag_hovered_id_ = hovered_id;
        }

        if (!previous_id.empty() && previous_id != origin_id) {
            post_event(previous_id,
                       {{"type", "dock.dragLeave"},
                        {"payload", {{"originWindowId", origin_id}}}});
        }
        if (!hovered_id.empty() && hovered_id != origin_id &&
            !payload.is_null()) {
            post_event(hovered_id,
                       {{"type", "dock.dragHover"},
                        {"payload", {{"originWindowId", origin_id}}}});
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
    std::atomic_uint next_id_{1};

    std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<webview::webview>> windows_;
    std::unordered_map<std::string, WindowInfo> window_info_;
    std::unordered_map<std::string, json> bootstraps_;
    json drag_payload_;
    std::string drag_origin_id_;
    std::string drag_hovered_id_;
    DragTracker drag_tracker_;
    BindingsSetup bindings_setup_;
};

} // namespace app
