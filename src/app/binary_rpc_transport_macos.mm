#include "app/binary_rpc_transport.h"
#include "app/binding_error.h"

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

#include <dispatch/dispatch.h>

#include <charconv>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace app::binary_rpc {
namespace {

constexpr std::string_view event_base = "app-rpc://native/event/";
constexpr std::size_t max_pending_events = 64;
constexpr std::size_t max_pending_event_bytes = std::size_t{32} * 1024 * 1024;
char authorized_view_key;

struct TransportState {
    Dispatcher dispatcher;
    std::map<std::uint64_t, Bytes> events;
    std::size_t pending_event_bytes = 0;
    std::uint64_t next_event_id = 1;
    std::string html;
};

// Scheme callbacks and window operations are serialized on the main queue.
std::shared_ptr<TransportState> active_state;

WKWebView *native_view(webview::webview &window) {
    const auto handle = window.browser_controller();
    return handle.ok() ? reinterpret_cast<WKWebView *>(handle.value()) : nil;
}

std::string utf8(NSString *value) {
    const char *data = value.UTF8String;
    return data ? data : "";
}

void reply(id<WKURLSchemeTask> task, Bytes body, NSInteger status,
           NSString *mime) {
    NSURL *url = task.request.URL;
    NSDictionary *headers = @{
        @"Content-Type" : mime,
        @"Cache-Control" : @"no-store",
        @"X-Content-Type-Options" : @"nosniff"
    };
    NSHTTPURLResponse *response = [[[NSHTTPURLResponse alloc]
        initWithURL:url
        statusCode:status
        HTTPVersion:@"HTTP/1.1"
        headerFields:headers] autorelease];
    NSData *data = [NSData dataWithBytes:body.data() length:body.size()];
    [task didReceiveResponse:response];
    if (data.length != 0) {
        [task didReceiveData:data];
    }
    [task didFinish];
}

Bytes request_body(NSURLRequest *request, bool &valid) {
    valid = true;
    NSData *data = request.HTTPBody;
    if (data) {
        if (data.length > max_message_size) {
            valid = false;
            return {};
        }
        const auto *begin = static_cast<const std::uint8_t *>(data.bytes);
        return Bytes(begin, begin + data.length);
    }
    NSInputStream *stream = request.HTTPBodyStream;
    if (!stream) {
        return {};
    }
    Bytes body;
    std::uint8_t chunk[8192];
    [stream open];
    while (true) {
        const NSInteger count = [stream read:chunk maxLength:sizeof(chunk)];
        if (count < 0 || static_cast<std::size_t>(count) >
                             max_message_size - body.size()) {
            valid = false;
            break;
        }
        if (count == 0) {
            break;
        }
        body.insert(body.end(), chunk, chunk + count);
    }
    [stream close];
    return body;
}

void process(id<WKURLSchemeTask> task, TransportState &state) {
    const auto uri = utf8(task.request.URL.absoluteString);
    const auto method = utf8(task.request.HTTPMethod);
    const auto caller = utf8([task.request valueForHTTPHeaderField:@"Origin"]);
    if (!caller.empty() && caller != "app-rpc://native" && caller != "null") {
        reply(task, {}, 403, @"application/octet-stream");
        return;
    }
    if (uri == page_url && method == "GET") {
        reply(task, Bytes(state.html.begin(), state.html.end()), 200,
              @"text/html; charset=utf-8");
        return;
    }
    if (method == "GET" && uri.starts_with(event_base)) {
        const auto text = std::string_view(uri).substr(event_base.size());
        std::uint64_t token = 0;
        const auto parsed =
            std::from_chars(text.data(), text.data() + text.size(), token);
        if (text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + text.size()) {
            reply(task, {}, 404, @"application/octet-stream");
            return;
        }
        auto event = state.events.find(token);
        if (event == state.events.end()) {
            reply(task, {}, 404, @"application/octet-stream");
            return;
        }
        Bytes body = std::move(event->second);
        state.pending_event_bytes -= body.size();
        state.events.erase(event);
        reply(task, std::move(body), 200, @"application/octet-stream");
        return;
    }
    if (method != "POST" || !uri.starts_with(rpc_base)) {
        reply(task, {}, 404, @"application/octet-stream");
        return;
    }
    const auto id_text = std::string_view(uri).substr(rpc_base.size());
    std::uint32_t id = 0;
    const auto parsed =
        std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
    if (id_text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != id_text.data() + id_text.size()) {
        reply(task, {}, 404, @"application/octet-stream");
        return;
    }
    bool valid = false;
    Bytes body = request_body(task.request, valid);
    if (!valid) {
        reply(task, {}, 413, @"application/octet-stream");
        return;
    }
    try {
        reply(task, state.dispatcher.call_enveloped(id, body), 200,
              @"application/octet-stream");
    } catch (const bindings::BindingError &error) {
        reply(task,
              error_response(static_cast<std::uint32_t>(error.code()),
                             error.what()),
              200, @"application/octet-stream");
    } catch (const WireError &error) {
        reply(task,
              error_response(static_cast<std::uint32_t>(
                                 bindings::ErrorCode::InvalidArgs),
                             error.what()),
              200, @"application/octet-stream");
    } catch (const std::exception &error) {
        reply(task,
              error_response(static_cast<std::uint32_t>(
                                 bindings::ErrorCode::InternalError),
                             error.what()),
              200, @"application/octet-stream");
    }
}

} // namespace
} // namespace app::binary_rpc

@interface AppBinarySchemeHandler : NSObject <WKURLSchemeHandler> {
  @private
    NSMutableSet *pending_;
    NSMutableSet *cancelled_;
}
@end

@implementation AppBinarySchemeHandler

- (instancetype)init {
    self = [super init];
    if (self) {
        pending_ = [[NSMutableSet alloc] init];
        cancelled_ = [[NSMutableSet alloc] init];
    }
    return self;
}

- (void)dealloc {
    [pending_ release];
    [cancelled_ release];
    [super dealloc];
}

- (void)webView:(WKWebView *)view startURLSchemeTask:(id<WKURLSchemeTask>)task {
    // The handler can receive callbacks off the UI thread. Native bindings and
    // the dispatcher use the same UI thread as the rest of the application.
    @synchronized(self) {
        [pending_ addObject:task];
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        @synchronized(self) {
            [pending_ removeObject:task];
            if ([cancelled_ containsObject:task]) {
                [cancelled_ removeObject:task];
                return;
            }
            auto state = app::binary_rpc::active_state;
            NSValue *authorized = (NSValue *)objc_getAssociatedObject(
                view, &app::binary_rpc::authorized_view_key);
            if (!state || !authorized ||
                authorized.pointerValue != static_cast<void *>(state.get())) {
                app::binary_rpc::reply(task, {}, 404,
                                       @"application/octet-stream");
                return;
            }
            app::binary_rpc::process(task, *state);
        }
    });
}

- (void)webView:(WKWebView *)view stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)view;
    @synchronized(self) {
        if ([pending_ containsObject:task]) {
            [cancelled_ addObject:task];
        }
    }
}

@end

namespace app::binary_rpc {

std::unique_ptr<webview::webview> create_webview(bool debug, void *parent) {
    return std::make_unique<webview::webview>(
        debug, parent, [](void *configuration) {
            auto *config = reinterpret_cast<WKWebViewConfiguration *>(
                configuration);
            auto *handler = [[AppBinarySchemeHandler alloc] init];
            [config setURLSchemeHandler:handler forURLScheme:@"app-rpc"];
            [handler release];
        });
}

bool install_transport(webview::webview &window, Dispatcher dispatcher) {
    auto *view = native_view(window);
    if (!view) {
        return false;
    }
    auto state = std::make_shared<TransportState>();
    state->dispatcher = std::move(dispatcher);
    objc_setAssociatedObject(
        view, &authorized_view_key, [NSValue valueWithPointer:state.get()],
        OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    active_state = std::move(state);
    return true;
}

void clear_transport(webview::webview &) {
    if (active_state) {
        active_state->dispatcher = Dispatcher{};
        active_state->events.clear();
        active_state->pending_event_bytes = 0;
        active_state.reset();
    }
}

bool shares_transport_context(webview::webview &first,
                              webview::webview &second) {
    return active_state && native_view(first) && native_view(second);
}

void authorize_view(webview::webview &window) {
    auto *view = native_view(window);
    if (!view || !active_state) {
        throw std::runtime_error("Failed to authorize binary RPC WebView");
    }
    objc_setAssociatedObject(
        view, &authorized_view_key,
        [NSValue valueWithPointer:active_state.get()],
        OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

bool post_event_bytes(webview::webview &window, Bytes &event) {
    if (!active_state || event.size() > max_message_size ||
        active_state->events.size() >= max_pending_events ||
        event.size() >
            max_pending_event_bytes - active_state->pending_event_bytes) {
        return false;
    }
    const auto token = active_state->next_event_id;
    if (token == 0 || active_state->events.contains(token)) {
        return false;
    }
    ++active_state->next_event_id;
    active_state->pending_event_bytes += event.size();
    active_state->events.emplace(token, std::move(event));
    try {
        window.eval("if(window.__APP_NATIVE_EVENT__)"
                    "window.__APP_NATIVE_EVENT__(" +
                    std::to_string(token) + ");");
    } catch (const std::exception &) {
        auto queued = active_state->events.find(token);
        if (queued != active_state->events.end()) {
            event = std::move(queued->second);
            active_state->pending_event_bytes -= event.size();
            active_state->events.erase(queued);
        }
        return false;
    }
    return true;
}

void load_html_with_binary_origin(webview::webview &window,
                                  const std::string &html) {
    if (!active_state) {
        window.set_html(html);
        return;
    }
    active_state->html = html;
    window.navigate(std::string(page_url));
}

} // namespace app::binary_rpc
