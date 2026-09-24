#include "app/binary_rpc_transport.h"
#include "app/binding_error.h"

#if defined(__linux__)
#include <gtk/gtk.h>
#if GTK_MAJOR_VERSION >= 4
#include <webkit/webkit.h>
#else
#include <webkit2/webkit2.h>
#endif

#include <array>
#include <charconv>
#include <memory>
#include <map>
#include <string_view>

namespace app::binary_rpc {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
namespace {

constexpr std::string_view event_prefix = "app-rpc://native/event/";
constexpr std::size_t chunk_size = 8192;
constexpr std::size_t max_pending_events = 64;
constexpr std::size_t max_pending_event_bytes = std::size_t{32} * 1024 * 1024;
constexpr const char *context_state_key = "app-binary-rpc-transport";
constexpr const char *authorized_view_key = "app-binary-rpc-authorized";

struct TransportState {
    Dispatcher dispatcher;
    std::map<std::uint64_t, Bytes> events;
    std::size_t pending_event_bytes = 0;
    std::uint64_t next_event_id = 1;
};

enum class HttpStatus : unsigned {
    ok = 200,
    bad_request = 400,
    not_found = 404,
    method_not_allowed = 405,
    payload_too_large = 413,
    server_error = 500
};

void finish(WebKitURISchemeRequest *request, Bytes body, HttpStatus status,
            const char *mime) {
    auto *owned = std::make_unique<Bytes>(std::move(body)).release();
    auto *bytes = g_bytes_new_with_free_func(
        owned->data(), owned->size(),
        +[](gpointer data) {
            std::unique_ptr<Bytes> owner(static_cast<Bytes *>(data));
        },
        owned);
    auto *stream = g_memory_input_stream_new_from_bytes(bytes);
    auto *response = webkit_uri_scheme_response_new(
        stream, static_cast<gint64>(owned->size()));
    webkit_uri_scheme_response_set_status(
        response, static_cast<unsigned>(status), nullptr);
    webkit_uri_scheme_response_set_content_type(response, mime);
    webkit_uri_scheme_request_finish_with_response(request, response);
    g_object_unref(response);
    g_object_unref(stream);
    g_bytes_unref(bytes);
}

void on_request(WebKitURISchemeRequest *request, gpointer data) {
    auto *state = static_cast<std::shared_ptr<TransportState> *>(data);
    auto *view = webkit_uri_scheme_request_get_web_view(request);
    if (!view || !g_object_get_data(G_OBJECT(view), authorized_view_key)) {
        finish(request, {}, HttpStatus::not_found,
               "application/octet-stream");
        return;
    }
    auto *headers = webkit_uri_scheme_request_get_http_headers(request);
    const char *origin = headers ? soup_message_headers_get_one(headers, "Origin")
                                 : nullptr;
    if (origin && std::string_view(origin) != "app-rpc://native" &&
        std::string_view(origin) != "null") {
        finish(request, {}, HttpStatus::not_found,
               "application/octet-stream");
        return;
    }
    const auto method =
        std::string_view(webkit_uri_scheme_request_get_http_method(request));
    const auto uri =
        std::string_view(webkit_uri_scheme_request_get_uri(request));
    if (method == "GET" && uri.starts_with(event_prefix)) {
        const auto text = uri.substr(event_prefix.size());
        std::uint64_t token = 0;
        const auto parsed =
            std::from_chars(text.data(), text.data() + text.size(), token);
        if (text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + text.size()) {
            finish(request, {}, HttpStatus::not_found,
                   "application/octet-stream");
            return;
        }
        auto event = (*state)->events.find(token);
        if (event == (*state)->events.end()) {
            finish(request, {}, HttpStatus::not_found,
                   "application/octet-stream");
            return;
        }
        Bytes body = std::move(event->second);
        (*state)->events.erase(event);
        (*state)->pending_event_bytes -= body.size();
        finish(request, std::move(body), HttpStatus::ok,
               "application/octet-stream");
        return;
    }
    if (method != "POST") {
        finish(request, {}, HttpStatus::method_not_allowed,
               "application/octet-stream");
        return;
    }
    if (!uri.starts_with(rpc_base)) {
        finish(request, {}, HttpStatus::not_found, "application/octet-stream");
        return;
    }
    const auto id_text = uri.substr(rpc_base.size());
    std::uint32_t id = 0;
    const auto parsed =
        std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
    if (id_text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != id_text.data() + id_text.size()) {
        finish(request, {}, HttpStatus::not_found, "application/octet-stream");
        return;
    }

    Bytes body;
    auto *stream = webkit_uri_scheme_request_get_http_body(request);
    if (stream) {
        if (headers) {
            const auto length = soup_message_headers_get_content_length(headers);
            if (length > static_cast<goffset>(max_message_size)) {
                finish(request, {}, HttpStatus::payload_too_large,
                       "application/octet-stream");
                return;
            }
            if (length > 0) {
                body.reserve(static_cast<std::size_t>(length));
            }
        }
        std::array<std::uint8_t, chunk_size> chunk{};
        while (true) {
            GError *error = nullptr;
            const auto count = g_input_stream_read(
                stream, chunk.data(), chunk.size(), nullptr, &error);
            if (count < 0) {
                g_clear_error(&error);
                finish(request, {}, HttpStatus::bad_request,
                       "application/octet-stream");
                return;
            }
            if (count == 0) {
                break;
            }
            if (static_cast<std::size_t>(count) >
                max_message_size - body.size()) {
                finish(request, {}, HttpStatus::payload_too_large,
                       "application/octet-stream");
                return;
            }
            const auto read =
                std::span(chunk).first(static_cast<std::size_t>(count));
            body.insert(body.end(), read.begin(), read.end());
        }
    }
    try {
        finish(request, (*state)->dispatcher.call_enveloped(id, body), HttpStatus::ok,
               "application/octet-stream");
    } catch (const bindings::BindingError &error) {
        finish(request,
               error_response(static_cast<std::uint32_t>(error.code()),
                              error.what()),
               HttpStatus::ok, "application/octet-stream");
    } catch (const WireError &error) {
        finish(request,
               error_response(
                   static_cast<std::uint32_t>(bindings::ErrorCode::InvalidArgs),
                   error.what()),
               HttpStatus::ok,
               "application/octet-stream");
    } catch (const std::exception &error) {
        finish(request,
               error_response(static_cast<std::uint32_t>(
                                  bindings::ErrorCode::InternalError),
                              error.what()),
               HttpStatus::ok,
               "application/octet-stream");
    }
}

} // namespace

bool install_transport(webview::webview &window, Dispatcher dispatcher) {
    auto handle = window.browser_controller();
    handle.ensure_ok();
    auto *view = WEBKIT_WEB_VIEW(handle.value());
    g_object_set_data(G_OBJECT(view), authorized_view_key, GINT_TO_POINTER(1));
    auto *context = webkit_web_view_get_context(view);
    auto *existing = static_cast<std::shared_ptr<TransportState> *>(
        g_object_get_data(G_OBJECT(context), context_state_key));
    if (existing) {
        (*existing)->dispatcher = std::move(dispatcher);
        return true;
    }
    auto state = std::make_shared<TransportState>();
    state->dispatcher = std::move(dispatcher);
    auto *security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(security, "app-rpc");
    webkit_web_context_register_uri_scheme(
        context, "app-rpc", on_request,
        std::make_unique<std::shared_ptr<TransportState>>(state).release(),
        +[](gpointer data) {
            std::unique_ptr<std::shared_ptr<TransportState>> owner(
                static_cast<std::shared_ptr<TransportState> *>(data));
        });
    g_object_set_data_full(
        G_OBJECT(context), context_state_key,
        std::make_unique<std::shared_ptr<TransportState>>(std::move(state))
            .release(),
        +[](gpointer data) {
            std::unique_ptr<std::shared_ptr<TransportState>> owner(
                static_cast<std::shared_ptr<TransportState> *>(data));
        });
    return true;
}

void authorize_view(webview::webview &window) {
    auto handle = window.browser_controller();
    handle.ensure_ok();
    g_object_set_data(G_OBJECT(handle.value()), authorized_view_key,
                      GINT_TO_POINTER(1));
}

void clear_transport(webview::webview &window) {
    auto handle = window.browser_controller();
    if (!handle.ok()) {
        return;
    }
    auto *context =
        webkit_web_view_get_context(WEBKIT_WEB_VIEW(handle.value()));
    auto *state = static_cast<std::shared_ptr<TransportState> *>(
        g_object_get_data(G_OBJECT(context), context_state_key));
    if (state) {
        (*state)->dispatcher = Dispatcher{};
        (*state)->events.clear();
        (*state)->pending_event_bytes = 0;
    }
}

bool post_event_bytes(webview::webview &window, Bytes &event) {
    if (event.size() > max_message_size) {
        return false;
    }
    auto handle = window.browser_controller();
    if (!handle.ok()) {
        return false;
    }
    auto *context =
        webkit_web_view_get_context(WEBKIT_WEB_VIEW(handle.value()));
    auto *slot = static_cast<std::shared_ptr<TransportState> *>(
        g_object_get_data(G_OBJECT(context), context_state_key));
    if (!slot || (*slot)->events.size() >= max_pending_events ||
        event.size() >
            max_pending_event_bytes - (*slot)->pending_event_bytes) {
        return false;
    }
    auto &state = **slot;
    const auto token = state.next_event_id;
    if (token == 0 || state.events.contains(token)) {
        return false;
    }
    ++state.next_event_id;
    state.pending_event_bytes += event.size();
    state.events.emplace(token, std::move(event));
    try {
        window.eval("if(window.__APP_NATIVE_EVENT__)"
                    "window.__APP_NATIVE_EVENT__(" +
                    std::to_string(token) + ");");
    } catch (const std::exception &) {
        auto queued = state.events.find(token);
        if (queued != state.events.end()) {
            event = std::move(queued->second);
            state.events.erase(queued);
            state.pending_event_bytes -= event.size();
        }
        return false;
    }
    return true;
}

bool shares_transport_context(webview::webview &first,
                              webview::webview &second) {
    auto first_handle = first.browser_controller();
    auto second_handle = second.browser_controller();
    first_handle.ensure_ok();
    second_handle.ensure_ok();
    return webkit_web_view_get_context(WEBKIT_WEB_VIEW(first_handle.value())) ==
           webkit_web_view_get_context(WEBKIT_WEB_VIEW(second_handle.value()));
}

void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    auto handle = window.browser_controller();
    handle.ensure_ok();
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(handle.value()), html.c_str(),
                              "app-rpc://native/index.html");
}
#else
bool install_transport(webview::webview &, Dispatcher) { return false; }
void clear_transport(webview::webview &) {}
bool shares_transport_context(webview::webview &, webview::webview &) {
    return false;
}
void authorize_view(webview::webview &) {}
bool post_event_bytes(webview::webview &, Bytes &) { return false; }
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    window.set_html(html);
}
#endif
} // namespace app::binary_rpc
#elif !defined(_WIN32) && !defined(__APPLE__)
namespace app::binary_rpc {
bool install_transport(webview::webview &, Dispatcher) { return false; }
void clear_transport(webview::webview &) {}
bool shares_transport_context(webview::webview &, webview::webview &) {
    return false;
}
void authorize_view(webview::webview &) {}
bool post_event_bytes(webview::webview &, Bytes &) { return false; }
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    window.set_html(html);
}
} // namespace app::binary_rpc
#endif
