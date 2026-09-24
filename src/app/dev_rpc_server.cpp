#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "app/binary_rpc_transport.h"
#include "app/binding_error.h"
#include "app/dev_rpc_server.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <future>
#include <random>
#include <span>
#include <string_view>
#include <utility>

namespace app {
namespace {

constexpr std::size_t max_headers = 8192;

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket socket) { close(socket); }
#endif

std::string make_token() {
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(32);
    for (int index = 0; index < 16; ++index) {
        const auto byte = static_cast<unsigned char>(random());
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 15]);
    }
    return token;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

std::string lower(std::string_view value) {
    std::string result(value);
    for (auto &letter : result) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
    }
    return result;
}

struct Request {
    std::string method;
    std::string path;
    std::string token;
    std::string origin;
    std::size_t content_length = 0;
    bool has_length = false;
    bool valid = false;
};

Request parse_headers(std::string_view headers) {
    Request request;
    const auto first_end = headers.find("\r\n");
    if (first_end == std::string_view::npos)
        return request;
    const auto line = headers.substr(0, first_end);
    const auto first_space = line.find(' ');
    const auto second_space = line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos ||
        second_space == std::string_view::npos ||
        line.substr(second_space + 1) != "HTTP/1.1")
        return request;
    request.method = line.substr(0, first_space);
    request.path = line.substr(first_space + 1, second_space - first_space - 1);
    auto position = first_end + 2;
    while (position < headers.size()) {
        const auto end = headers.find("\r\n", position);
        if (end == std::string_view::npos)
            return request;
        if (end == position)
            break;
        const auto field = headers.substr(position, end - position);
        const auto colon = field.find(':');
        if (colon == std::string_view::npos)
            return request;
        const auto key = lower(field.substr(0, colon));
        const auto value = trim(field.substr(colon + 1));
        if (key == "x-app-rpc-token")
            request.token = value;
        if (key == "origin")
            request.origin = value;
        if (key == "content-length") {
            if (request.has_length)
                return request;
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(),
                                request.content_length);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size())
                return request;
            request.has_length = true;
        }
        if (key == "transfer-encoding")
            return request;
        position = end + 2;
    }
    request.valid = !request.path.empty() && request.path.front() == '/';
    return request;
}

bool send_all(Socket socket, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const char *>(data);
    while (size > 0) {
        const auto count =
            send(socket, bytes,
#if defined(_WIN32)
                 static_cast<int>((std::min)(size, std::size_t{32768})), 0);
#else
                 (std::min)(size, std::size_t{32768}), 0);
#endif
        if (count <= 0)
            return false;
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}

void reply(Socket socket, int status, binary_rpc::Bytes body = {}) {
    const char *reason = status == 200 ? "OK" : "Error";
    const auto header =
        "HTTP/1.1 " + std::to_string(status) + " " + reason +
        "\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
        std::to_string(body.size()) +
        "\r\nCache-Control: no-store\r\nX-Content-Type-Options: "
        "nosniff\r\nConnection: close\r\n\r\n";
    if (send_all(socket, header.data(), header.size()) && !body.empty()) {
        (void)send_all(socket, body.data(), body.size());
    }
}

} // namespace

DevRpcServer::DevRpcServer(webview::webview &window,
                           binary_rpc::Dispatcher dispatcher, int port,
                           std::string expected_origin)
    : window_(window), dispatcher_(std::move(dispatcher)),
      port_(port), expected_origin_(std::move(expected_origin)),
      token_(make_token()) {}

DevRpcServer::~DevRpcServer() {
    stopping_ = true;
    alive_->store(false);
    if (listener_ != 0) {
        auto socket = static_cast<Socket>(listener_);
        shutdown(socket,
#if defined(_WIN32)
                 SD_BOTH
#else
                 SHUT_RDWR
#endif
        );
        close_socket(socket);
    }
    if (thread_.joinable())
        thread_.join();
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool DevRpcServer::start() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return false;
#endif
    const Socket socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == invalid_socket)
        return false;
#if !defined(_WIN32)
    const int flags = fcntl(socket, F_GETFD);
    if (flags < 0 || fcntl(socket, F_SETFD, flags | FD_CLOEXEC) < 0) {
        close_socket(socket);
        return false;
    }
#endif
#if defined(_WIN32)
    int exclusive = 1;
    SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT,
                         0);
    setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
#else
    int reuse = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port_));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) !=
            0 ||
        listen(socket, 8) != 0) {
        close_socket(socket);
        return false;
    }
    listener_ = static_cast<std::uintptr_t>(socket);
    thread_ = std::thread([this] { serve(); });
    return true;
}

void DevRpcServer::serve() {
    while (!stopping_) {
        const Socket client =
            accept(static_cast<Socket>(listener_), nullptr, nullptr);
        if (client == invalid_socket)
            break;
#if defined(_WIN32)
        DWORD timeout = 10000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
        timeval timeout{10, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        handle_client(static_cast<std::uintptr_t>(client));
        close_socket(client);
    }
}

void DevRpcServer::handle_client(std::uintptr_t raw_socket) {
    const Socket socket = static_cast<Socket>(raw_socket);
    std::string received;
    received.reserve(max_headers);
    std::size_t header_end = std::string::npos;
    while (received.size() < max_headers &&
           (header_end = received.find("\r\n\r\n")) == std::string::npos) {
        std::array<char, 4096> chunk{};
        const auto count =
            recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (count <= 0)
            return;
        received.append(chunk.data(), static_cast<std::size_t>(count));
    }
    if (header_end == std::string::npos || header_end > max_headers) {
        reply(socket, 400);
        return;
    }
    const auto request =
        parse_headers(std::string_view(received).substr(0, header_end + 2));
    if (!request.valid) {
        reply(socket, 400);
        return;
    }
    if (request.token != token_ ||
        (!request.origin.empty() && request.origin != expected_origin_)) {
        reply(socket, 403);
        return;
    }
    if (request.content_length > binary_rpc::max_message_size) {
        reply(socket, 413);
        return;
    }
    const auto path = std::string_view(request.path);
    const bool event_request = path.starts_with("/event/");
    if ((event_request && request.method != "GET") ||
        (!event_request && (request.method != "POST" || !request.has_length))) {
        reply(socket, 405);
        return;
    }
    auto body = binary_rpc::Bytes(
        received.begin() + static_cast<std::ptrdiff_t>(header_end + 4),
        received.end());
    if (body.size() > request.content_length) {
        reply(socket, 400);
        return;
    }
    while (body.size() < request.content_length) {
        std::array<std::uint8_t, 8192> chunk{};
        const auto count =
            recv(socket, reinterpret_cast<char *>(chunk.data()),
#if defined(_WIN32)
                 static_cast<int>((std::min)(
                     chunk.size(), request.content_length - body.size())),
                 0);
#else
                 (std::min)(chunk.size(), request.content_length - body.size()),
                 0);
#endif
        if (count <= 0)
            return;
        body.insert(body.end(), chunk.begin(), chunk.begin() + count);
    }
    const auto number = path.substr(event_request ? 7 : 1);
    std::uint64_t id = 0;
    const auto parsed =
        std::from_chars(number.data(), number.data() + number.size(), id);
    if (number.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != number.data() + number.size() ||
        (!event_request && id > UINT32_MAX)) {
        reply(socket, 404);
        return;
    }
    auto promise = std::make_shared<std::promise<binary_rpc::Bytes>>();
    auto future = promise->get_future();
    try {
        window_.dispatch([this, alive = alive_, promise, id, event_request,
                          body = std::move(body)] {
            if (!alive->load()) return;
            try {
                if (event_request) {
                    promise->set_value(
                        binary_rpc::take_event_bytes(window_, id));
                } else {
                    promise->set_value(dispatcher_.call_enveloped(
                        static_cast<std::uint32_t>(id), body));
                }
            } catch (const bindings::BindingError &error) {
                promise->set_value(binary_rpc::error_response(
                    static_cast<std::uint32_t>(error.code()), error.what()));
            } catch (const binary_rpc::WireError &error) {
                promise->set_value(binary_rpc::error_response(
                    static_cast<std::uint32_t>(
                        bindings::ErrorCode::InvalidArgs),
                    error.what()));
            } catch (const std::exception &error) {
                promise->set_value(binary_rpc::error_response(
                    static_cast<std::uint32_t>(
                        bindings::ErrorCode::InternalError),
                    error.what()));
            }
        });
        if (future.wait_for(std::chrono::seconds(10)) !=
            std::future_status::ready) {
            reply(socket, 503);
            return;
        }
        auto result = future.get();
        reply(socket, event_request && result.empty() ? 404 : 200,
              std::move(result));
    } catch (const std::exception &) {
        reply(socket, 500);
    }
}

} // namespace app
