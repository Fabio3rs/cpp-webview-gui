#include "app/binary_rpc_transport.h"

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
#include <string_view>

namespace app::binary_rpc {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
namespace {

constexpr std::string_view prefix = "app-rpc://native/";
constexpr std::size_t chunk_size = 8192;

enum class HttpStatus : unsigned {
    ok = 200,
    bad_request = 400,
    not_found = 404,
    method_not_allowed = 405,
    payload_too_large = 413
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
    auto *dispatcher = static_cast<Dispatcher *>(data);
    const auto method =
        std::string_view(webkit_uri_scheme_request_get_http_method(request));
    if (method != "POST") {
        finish(request, {}, HttpStatus::method_not_allowed,
               "application/octet-stream");
        return;
    }
    const auto uri =
        std::string_view(webkit_uri_scheme_request_get_uri(request));
    if (!uri.starts_with(prefix)) {
        finish(request, {}, HttpStatus::not_found, "application/octet-stream");
        return;
    }
    const auto id_text = uri.substr(prefix.size());
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
        finish(request, dispatcher->call(id, body), HttpStatus::ok,
               "application/octet-stream");
    } catch (const std::exception &error) {
        const auto message = std::string_view(error.what());
        finish(request, Bytes(message.begin(), message.end()),
               HttpStatus::bad_request, "text/plain; charset=utf-8");
    }
}

} // namespace

bool install_transport(webview::webview &window, Dispatcher dispatcher) {
    auto handle = window.browser_controller();
    handle.ensure_ok();
    auto *view = WEBKIT_WEB_VIEW(handle.value());
    auto *context = webkit_web_view_get_context(view);
    auto *security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(security, "app-rpc");
    webkit_web_context_register_uri_scheme(
        context, "app-rpc", on_request,
        std::make_unique<Dispatcher>(std::move(dispatcher)).release(),
        +[](gpointer data) {
            std::unique_ptr<Dispatcher> owner(static_cast<Dispatcher *>(data));
        });
    return true;
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
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    window.set_html(html);
}
#endif
} // namespace app::binary_rpc
#else
namespace app::binary_rpc {
bool install_transport(webview::webview &, Dispatcher) { return false; }
void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    window.set_html(html);
}
} // namespace app::binary_rpc
#endif
