//
// The setup portal's web server and form helpers.
//
// Requests are read into a buffer until the headers and any body are
// whole, answered in one piece, and the connection is closed. A phone
// opens several connections at once and leaves some of them idle, so
// the number held is capped and the quietest is dropped to make room,
// rather than refusing the one the phone is actually waiting on.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/portal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace disciple {
namespace {

// The setup form is a few hundred bytes. Anything larger is not it.
constexpr std::size_t request_limit = 16 * 1024;

// Connections held at once. A phone uses four or five.
constexpr std::size_t client_limit = 8;

const char *reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 302:
        return "Found";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 413:
        return "Payload Too Large";
    default:
        return "Error";
    }
}

//
// Case-insensitive comparison for header names.
//
bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

//
// Parse a request once it is whole.
//
// Returns:
//      1 when parsed, 0 when more bytes are needed, -1 when it is not
//      a request this server will answer.
//
int parse_request(const std::string &raw, http_request &out) {
    const std::size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos) {
        return raw.size() > request_limit ? -1 : 0;
    }

    std::string_view head(raw.data(), end);
    const std::size_t line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end);

    const std::size_t first = line.find(' ');
    const std::size_t second = line.find(' ', first + 1);
    if (first == std::string_view::npos ||
        second == std::string_view::npos) {
        return -1;
    }
    out.method = std::string(line.substr(0, first));
    const std::string_view target =
        line.substr(first + 1, second - first - 1);
    const std::size_t question = target.find('?');
    out.path = std::string(target.substr(0, question));
    out.query = question == std::string_view::npos
                    ? std::string()
                    : std::string(target.substr(question + 1));

    std::size_t length = 0;
    std::string_view rest = line_end == std::string_view::npos
                                ? std::string_view()
                                : head.substr(line_end + 2);
    while (!rest.empty()) {
        const std::size_t next = rest.find("\r\n");
        const std::string_view header = rest.substr(0, next);
        rest = next == std::string_view::npos ? std::string_view()
                                              : rest.substr(next + 2);

        const std::size_t colon = header.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view name = header.substr(0, colon);
        const std::string_view value = trim(header.substr(colon + 1));
        if (same_name(name, "Host")) {
            out.host = std::string(value);
        } else if (same_name(name, "Content-Length")) {
            length = std::strtoul(std::string(value).c_str(), nullptr, 10);
        }
    }

    if (length > request_limit) {
        return -1;
    }
    const std::size_t body_start = end + 4;
    if (raw.size() < body_start + length) {
        return 0;
    }
    out.body = raw.substr(body_start, length);
    return 1;
}

std::string serialise(const http_reply &reply) {
    std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " " +
                      reason(reply.status) + "\r\n";
    out += "Content-Type: " + reply.content_type + "\r\n";
    out += "Content-Length: " + std::to_string(reply.body.size()) + "\r\n";
    // A captive portal check must never be answered from a cache.
    out += "Cache-Control: no-store\r\n";
    out += "Connection: close\r\n";
    if (!reply.location.empty()) {
        out += "Location: " + reply.location + "\r\n";
    }
    out += "\r\n";
    out += reply.body;
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

//
// Decode one form-encoded value: '+' is a space and %XX is a byte.
//
std::string url_decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() &&
                   hex_digit(text[i + 1]) >= 0 &&
                   hex_digit(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_digit(text[i + 1]) * 16 +
                                     hex_digit(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

std::string form_value(std::string_view body, std::string_view key) {
    while (!body.empty()) {
        const std::size_t amp = body.find('&');
        const std::string_view pair = body.substr(0, amp);
        body = amp == std::string_view::npos ? std::string_view()
                                             : body.substr(amp + 1);

        const std::size_t equals = pair.find('=');
        if (url_decode(pair.substr(0, equals)) == key) {
            return equals == std::string_view::npos
                       ? std::string()
                       : url_decode(pair.substr(equals + 1));
        }
    }
    return "";
}

std::string html_escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

bool http_portal::open(inkerface::network &net, int port,
                       handler on_request, std::string &error) {
    close();
    _listener.emplace(net, static_cast<std::uint16_t>(port));
    if (!*_listener) {
        error = "cannot serve the setup page on port " + std::to_string(port) +
                (_listener->error() == inkerface::net_error::bind
                     ? ": the port is taken or not permitted"
                     : "");
        _listener.reset();
        return false;
    }
    _handler = std::move(on_request);
    return true;
}

bool http_portal::is_open() const {
    return _listener.has_value();
}

void http_portal::close() {
    _clients.clear();
    _listener.reset();
}

void http_portal::poll() {
    if (!_listener) {
        return;
    }
    accept_all();

    // Served by index: the handler may close the portal, which empties
    // the list, and a finished client is removed as it goes.
    for (std::size_t i = 0; i < _clients.size();) {
        if (serve(_clients[i])) {
            if (_clients.empty()) {
                return;
            }
            _clients.erase(_clients.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void http_portal::accept_all() {
    while (_listener) {
        inkerface::tcp_socket peer = _listener->accept();
        if (!peer.is_open()) {
            return;
        }
        // At the limit, the connection heard from longest ago makes room.
        // A phone opens several and leaves some idle; the one it is
        // waiting on must not be the one refused.
        if (_clients.size() >= client_limit) {
            const auto quietest = std::min_element(
                _clients.begin(), _clients.end(),
                [](const client &a, const client &b) {
                    return a.last_heard < b.last_heard;
                });
            _clients.erase(quietest);
        }
        _clients.push_back(
            client{std::move(peer), {}, std::chrono::steady_clock::now()});
    }
}

//
// Read what one client has sent and answer it once it is whole.
//
// Returns:
//      True when the client is finished with, answered or gone.
//
bool http_portal::serve(client &peer) {
    std::array<std::byte, 1024> buffer{};
    for (;;) {
        const std::size_t got =
            peer.link.read(buffer, std::chrono::milliseconds(0));
        if (got == 0) {
            break;
        }
        peer.inbound.append(reinterpret_cast<const char *>(buffer.data()), got);
        peer.last_heard = std::chrono::steady_clock::now();
    }

    http_request request;
    const int state = parse_request(peer.inbound, request);
    if (state == 0) {
        return !peer.link.is_open();
    }

    http_reply reply;
    if (state < 0) {
        reply.status = peer.inbound.size() > request_limit ? 413 : 400;
        reply.content_type = "text/plain";
        reply.body = "not a request this reader understands\n";
    } else {
        reply = _handler(request);
        if (!_listener) {
            // The handler closed the portal; this client went with it.
            return true;
        }
    }

    // A reply is a few kilobytes, which goes in one write, so there is
    // no outbound queue to keep. Done either way afterwards.
    static_cast<void>(peer.link.write(serialise(reply)));
    return true;
}

} // namespace disciple
