//
// The loopback ingest listener.
//
// One thread, non-blocking sockets, and a connection object per open
// descriptor. Nothing here blocks, because a stalled Evangelist must
// not be able to stop the daemon serving anyone else.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/http_server.h"

#include "apostle/log.h"
#include "apostle/version.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace apostle {
namespace {

// Read this much from a socket at a time. A frame is megabytes, so a
// small buffer would mean thousands of trips through the poll loop.
constexpr std::size_t read_chunk = 64 * 1024;

// The headers every reply carries. Loopback only, so the permissive
// origin costs nothing and makes a curl or devtools test work.
constexpr const char *cors_headers =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, X-Tabcast-Sequence, "
    "X-Tabcast-Session, X-Tabcast-Width, X-Tabcast-Height, "
    "X-Tabcast-Title, X-Tabcast-Url\r\n";

//
// The reason phrase for a status this daemon actually sends.
//
const char *reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 415:
        return "Unsupported Media Type";
    case 431:
        return "Request Header Fields Too Large";
    default:
        return "Error";
    }
}

//
// Quote a string for a JSON value.
//
// Notes:
//      Only the escapes JSON requires, and control characters as
//      \u00xx. Titles arrive from a web page, so this cannot assume
//      anything about what is in them.
//
std::string json_quote(const std::string &text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');

    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char digits[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(c >> 4) & 0x0f]);
                out.push_back(digits[c & 0x0f]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }

    out.push_back('"');
    return out;
}

//
// Put a descriptor into non-blocking mode.
//
bool set_non_blocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

//
// One open connection and the request being read on it.
//
struct http_server::connection {
    explicit connection(int fd)
        : descriptor(fd)
        , parser(static_cast<std::size_t>(max_frame_bytes)) {
    }

    int descriptor;
    request_parser parser;

    // Set once a reply has been written. The connection is closed
    // afterwards: this is HTTP/1.1 without keep-alive, which costs a
    // socket per frame on loopback and saves the whole class of bugs
    // that comes with reusing one.
    bool answered = false;
};

http_server::http_server(frame_store &frames)
    : _frames(frames) {
}

http_server::~http_server() {
    close();
}

bool http_server::open(int port, std::string &error) {
    _listener = socket(AF_INET, SOCK_STREAM, 0);
    if (_listener < 0) {
        error = std::string("cannot create socket: ") +
                std::strerror(errno);
        return false;
    }

    // So a restart does not have to wait out TIME_WAIT on the port.
    const int reuse = 1;
    setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, &reuse,
               sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    // Loopback and nothing else. See the note in the header.
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(_listener, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0) {
        error = "cannot bind 127.0.0.1:" + std::to_string(port) + ": " +
                std::strerror(errno);
        ::close(_listener);
        _listener = -1;
        return false;
    }

    if (listen(_listener, 16) < 0) {
        error = std::string("cannot listen: ") + std::strerror(errno);
        ::close(_listener);
        _listener = -1;
        return false;
    }

    if (!set_non_blocking(_listener)) {
        error = std::string("cannot set the socket non-blocking: ") +
                std::strerror(errno);
        ::close(_listener);
        _listener = -1;
        return false;
    }

    return true;
}

void http_server::close() {
    _connections.clear();
    if (_listener >= 0) {
        ::close(_listener);
        _listener = -1;
    }
}

void http_server::watch(std::vector<pollfd> &out) const {
    if (_listener >= 0) {
        out.push_back(pollfd{_listener, POLLIN, 0});
    }
    for (const auto &entry : _connections) {
        out.push_back(pollfd{entry.first, POLLIN, 0});
    }
}

void http_server::ready(int descriptor) {
    if (descriptor < 0) {
        return;
    }
    if (descriptor == _listener) {
        accept_one();
        return;
    }

    const auto found = _connections.find(descriptor);
    if (found != _connections.end()) {
        service(*found->second);
    }
}

void http_server::on_frame(std::function<void(const frame &)> handler) {
    _on_frame = std::move(handler);
}

void http_server::client_counter(std::function<int()> counter) {
    _client_counter = std::move(counter);
}

int http_server::client_count() const {
    return _client_counter ? _client_counter() : 0;
}

//
// Take every connection the backlog is holding.
//
void http_server::accept_one() {
    for (;;) {
        const int client = accept(_listener, nullptr, nullptr);
        if (client < 0) {
            // EAGAIN means the backlog is drained, which is the normal
            // way out of this loop.
            if (errno != EAGAIN && errno != EWOULDBLOCK &&
                errno != EINTR) {
                log_write(log_level::warn, "accept failed: {}",
                          std::strerror(errno));
            }
            return;
        }

        if (!set_non_blocking(client)) {
            ::close(client);
            continue;
        }

        // Frames are posted as one burst and answered immediately, so
        // waiting to coalesce a reply only adds latency.
        const int nodelay = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                   sizeof(nodelay));

        _connections.emplace(client,
                             std::make_unique<connection>(client));
        log_write(log_level::debug, "ingest connection {} opened",
                  client);
    }
}

//
// Read what is available on a connection and answer once a whole
// request has arrived.
//
void http_server::service(connection &link) {
    std::uint8_t buffer[read_chunk];

    for (;;) {
        const ssize_t got = recv(link.descriptor, buffer, sizeof(buffer),
                                 0);
        if (got == 0) {
            // The peer closed before finishing. Nothing to answer.
            drop(link.descriptor);
            return;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            log_write(log_level::debug, "read on {} failed: {}",
                      link.descriptor, std::strerror(errno));
            drop(link.descriptor);
            return;
        }

        const parse_result state =
            link.parser.feed(buffer, static_cast<std::size_t>(got));

        if (state == parse_result::failed) {
            log_write(log_level::warn, "rejected a request: {}",
                      link.parser.error());
            respond(link, link.parser.status(),
                    "{\"ok\":false,\"error\":" +
                        json_quote(link.parser.error()) + "}");
            drop(link.descriptor);
            return;
        }
        if (state == parse_result::done) {
            handle_request(link);
            drop(link.descriptor);
            return;
        }
    }
}

//
// Route a complete request.
//
void http_server::handle_request(connection &link) {
    const http_request &request = link.parser.request();

    // The target may carry a query string. Nothing here uses one, but
    // matching on the path alone keeps a stray ?t=1 from 404ing.
    std::string path = request.target;
    const std::size_t query = path.find('?');
    if (query != std::string::npos) {
        path.resize(query);
    }

    if (request.method == "OPTIONS") {
        respond(link, 204, std::string());
        return;
    }

    if (path == "/api/v1/frame") {
        if (request.method != "POST") {
            respond(link, 405,
                    "{\"ok\":false,\"error\":\"use POST\"}");
            return;
        }
        handle_frame(link);
        return;
    }

    if (path == "/api/v1/health") {
        if (request.method != "GET") {
            respond(link, 405,
                    "{\"ok\":false,\"error\":\"use GET\"}");
            return;
        }
        handle_health(link);
        return;
    }

    respond(link, 404,
            "{\"ok\":false,\"error\":\"no such endpoint\"}");
}

//
// Accept one uploaded frame.
//
void http_server::handle_frame(connection &link) {
    const http_request &request = link.parser.request();

    const std::string *type = request.header("Content-Type");
    if (type == nullptr) {
        respond(link, 400,
                "{\"ok\":false,\"error\":\"missing Content-Type\"}");
        return;
    }
    if (*type != "image/png" && *type != "image/jpeg") {
        respond(link, 415,
                "{\"ok\":false,\"error\":\"expected image/png or "
                "image/jpeg\"}");
        return;
    }

    // Required headers, per docs/PROTOCOL.md. A frame missing any of
    // them is refused rather than stored half described, because the
    // broadcast half will need every one of them.
    struct required {
        const char *name;
        long long *value;
    };

    long long sequence = 0;
    long long width = 0;
    long long height = 0;
    const required numbers[] = {{"X-Tabcast-Sequence", &sequence},
                                {"X-Tabcast-Width", &width},
                                {"X-Tabcast-Height", &height}};

    for (const required &entry : numbers) {
        const std::string *raw = request.header(entry.name);
        if (raw == nullptr) {
            respond(link, 400,
                    "{\"ok\":false,\"error\":\"missing " +
                        std::string(entry.name) + "\"}");
            return;
        }
        if (!parse_number(*raw, *entry.value)) {
            respond(link, 400,
                    "{\"ok\":false,\"error\":\"malformed " +
                        std::string(entry.name) + "\"}");
            return;
        }
    }

    const std::string *session = request.header("X-Tabcast-Session");
    if (session == nullptr || session->empty()) {
        respond(link, 400,
                "{\"ok\":false,\"error\":\"missing "
                "X-Tabcast-Session\"}");
        return;
    }

    if (request.body.empty()) {
        respond(link, 400, "{\"ok\":false,\"error\":\"empty frame\"}");
        return;
    }

    frame captured;
    captured.bytes = request.body;
    captured.mime_type = *type;
    captured.sequence = sequence;
    captured.session = *session;
    captured.width = static_cast<int>(width);
    captured.height = static_cast<int>(height);

    if (const std::string *title = request.header("X-Tabcast-Title")) {
        captured.title = percent_decode(*title);
    }
    if (const std::string *url = request.header("X-Tabcast-Url")) {
        captured.url = percent_decode(*url);
    }

    const std::size_t size = captured.bytes.size();
    const std::string title = captured.title;
    _frames.put(std::move(captured));

    log_write(log_level::debug,
              "frame {} accepted, {}x{}, {} bytes, {}", sequence, width,
              height, size, title.empty() ? "(no title)" : title);

    // Straight on to the flock, from the stored copy so the handler
    // sees exactly what was kept.
    if (_on_frame) {
        if (const frame *stored = _frames.newest()) {
            _on_frame(*stored);
        }
    }

    respond(link, 200,
            "{\"ok\":true,\"sequence\":" + std::to_string(sequence) +
                ",\"clients\":" + std::to_string(client_count()) + "}");
}

//
// Answer the liveness probe.
//
void http_server::handle_health(connection &link) {
    respond(link, 200,
            std::string("{\"ok\":true,\"name\":\"apostle\",\"version\":") +
                json_quote(version) +
                ",\"protocol\":" + std::to_string(protocol_version) +
                ",\"clients\":" + std::to_string(client_count()) +
                ",\"maxFrameBytes\":" + std::to_string(max_frame_bytes) +
                "}");
}

//
// Write one reply.
//
// Notes:
//      Writes are blocking-ish: the loop keeps going until the whole
//      reply is out or the peer is gone. Replies here are a few
//      hundred bytes and go over loopback, so they fit in the socket
//      buffer in one call in practice, and carrying a partial-write
//      queue for that would be machinery without a purpose.
//
void http_server::respond(connection &link, int status,
                          const std::string &json) {
    std::string reply = "HTTP/1.1 " + std::to_string(status) + " " +
                        reason_phrase(status) + "\r\n";
    reply += cors_headers;
    reply += "Connection: close\r\n";

    if (json.empty()) {
        reply += "Content-Length: 0\r\n\r\n";
    } else {
        reply += "Content-Type: application/json\r\n";
        reply += "Content-Length: " + std::to_string(json.size()) +
                 "\r\n\r\n";
        reply += json;
    }

    std::size_t sent = 0;
    while (sent < reply.size()) {
        const ssize_t wrote = send(link.descriptor, reply.data() + sent,
                                   reply.size() - sent, MSG_NOSIGNAL);
        if (wrote > 0) {
            sent += static_cast<std::size_t>(wrote);
            continue;
        }
        if (wrote < 0 && (errno == EINTR || errno == EAGAIN ||
                          errno == EWOULDBLOCK)) {
            continue;
        }
        log_write(log_level::debug, "reply to {} failed: {}",
                  link.descriptor, std::strerror(errno));
        break;
    }

    link.answered = true;
}

//
// Close a connection and forget it.
//
void http_server::drop(int descriptor) {
    const auto found = _connections.find(descriptor);
    if (found == _connections.end()) {
        return;
    }
    ::close(descriptor);
    _connections.erase(found);
    log_write(log_level::debug, "ingest connection {} closed",
              descriptor);
}

} // namespace apostle
