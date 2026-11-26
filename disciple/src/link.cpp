//
// The reader's connection to Apostle.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/link.h"

#include "disciple/version.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

namespace disciple {
namespace {

using namespace std::chrono_literals;

// A daemon on the LAN accepts at once. One that takes longer than this
// is not going to.
constexpr auto connect_timeout = 5s;

// How much to ask the socket for at a time. Read straight into the
// inbound buffer, never onto the stack: a reader's main task has a stack
// of a few kilobytes.
constexpr std::size_t read_chunk = 4 * 1024;

//
// A net_error in words.
//
std::string describe(inkerface::net_error error) {
    switch (error) {
    case inkerface::net_error::none:
        return "";
    case inkerface::net_error::not_connected:
        return "not on a network";
    case inkerface::net_error::invalid_address:
        return "not a usable address";
    case inkerface::net_error::resolve:
        return "name not found";
    case inkerface::net_error::connect:
        return "connection failed";
    case inkerface::net_error::tls_unavailable:
        return "no TLS in this build";
    case inkerface::net_error::closed:
        return "closed by apostle";
    case inkerface::net_error::cancelled:
        return "cancelled";
    case inkerface::net_error::too_large:
        return "too large";
    case inkerface::net_error::incomplete:
        return "cut short";
    case inkerface::net_error::bind:
        return "port in use";
    }
    return "network error";
}

} // namespace

std::string hello_payload(const reader_identity &who) {
    return std::string("{\"name\":\"disciple\",\"version\":") +
           json_quoted(version) + ",\"protocol\":" +
           std::to_string(protocol_version) +
           ",\"device\":" + json_quoted(who.name) +
           ",\"width\":" + std::to_string(who.width) +
           ",\"height\":" + std::to_string(who.height) +
           ",\"grayLevels\":" + std::to_string(reader_gray_levels) +
           ",\"accepts\":[\"image/gray-packed\"]}";
}

void apostle_link::reserve(std::size_t bytes) {
    _inbound.reserve(bytes);
}

bool apostle_link::open(inkerface::network &net, const std::string &host,
                        int port, const reader_identity &who,
                        std::string &error) {
    close();
    _peer = host + ":" + std::to_string(port);
    if (port < 1 || port > 65535) {
        error = "not a port: " + std::to_string(port);
        return false;
    }

    inkerface::socket_options options;
    options.timeout = connect_timeout;
    _socket.emplace(net, host, static_cast<std::uint16_t>(port), options);
    if (!*_socket) {
        error = "cannot reach apostle at " + _peer + ": " +
                describe(_socket->error());
        _socket.reset();
        return false;
    }
    return send(message_type::hello, hello_payload(who), error);
}

bool apostle_link::is_open() const {
    return _socket.has_value();
}

bool apostle_link::joined() const {
    return _joined;
}

int apostle_link::keepalive_seconds() const {
    return _keepalive;
}

const std::string &apostle_link::client_id() const {
    return _client_id;
}

const std::string &apostle_link::peer() const {
    return _peer;
}

//
// Take the client id and keepalive from the welcome.
//
// Notes:
//      A keepalive that is missing or absurd falls back to the protocol
//      default rather than refusing the daemon. It only sizes a timeout.
//
void apostle_link::take_welcome(std::string_view text) {
    _client_id.clear();
    json_string(text, "clientId", _client_id);

    long long keepalive = 0;
    _keepalive = json_number(text, "keepalive", keepalive) && keepalive > 0 &&
                         keepalive <= 3600
                     ? static_cast<int>(keepalive)
                     : default_keepalive_seconds;
    _joined = true;
}

bool apostle_link::read_ready(const frame_handler &on_frame, bool &heard,
                              std::string &error) {
    heard = false;
    if (!_socket) {
        error = "not connected";
        return false;
    }

    // Everything that has arrived, without waiting. An end of stream is
    // noted, not acted on at once: Apostle says bye, or sends a last
    // frame, and closes straight after, so the same pass that sees the
    // close often holds those too, and they are worth having.
    for (;;) {
        // Read into the room already reserved, so the buffer does not
        // grow; only a message larger than reserved makes it.
        const std::size_t held = _inbound.size();
        const std::size_t room = _inbound.capacity() - held;
        const std::size_t chunk = room >= 512 ? std::min(room, read_chunk)
                                              : read_chunk;
        _inbound.resize(held + chunk);
        const std::size_t got = _socket->read(
            std::as_writable_bytes(std::span(_inbound.data() + held, chunk)),
            0ms);
        _inbound.resize(held + got);
        if (got == 0) {
            break;
        }
        heard = true;
    }
    const bool ended = !_socket->is_open();
    if (ended && _socket->error() != inkerface::net_error::closed) {
        error = "link failed: " + describe(_socket->error());
        return false;
    }

    for (;;) {
        message_type type = message_type::ping;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::string problem;
        const decode_result state = decode_message(
            _inbound.data(), _inbound.size(), type, offset, size, problem);
        if (state == decode_result::need_more) {
            break;
        }
        if (state == decode_result::failed) {
            error = problem;
            return false;
        }

        const std::uint8_t *payload = _inbound.data() + offset;
        const std::string_view text(reinterpret_cast<const char *>(payload),
                                    size);

        if (type == message_type::bye) {
            std::string reason;
            json_string(text, "reason", reason);
            if (!_joined) {
                error = reason.empty() ? "apostle refused the connection"
                                       : "apostle refused us: " + reason;
            } else {
                error = reason.empty() ? std::string() : "apostle: " + reason;
            }
            return false;
        }

        if (!_joined) {
            // Anything but a welcome first is a daemon that does not
            // follow its own protocol.
            if (type != message_type::welcome) {
                error = std::string("expected welcome, got ") +
                        message_type_name(type);
                return false;
            }
            take_welcome(text);
        } else if (type == message_type::frame) {
            frame_header header;
            if (!decode_frame_header(payload, size, header)) {
                error = "a frame arrived with a malformed header";
                return false;
            }
            // Drawn from where it lies, before it is erased.
            on_frame(header, std::span(payload + frame_header_bytes,
                                       size - frame_header_bytes));
        } else if (type == message_type::ping) {
            if (!send(message_type::pong, "", error)) {
                return false;
            }
        }
        // pong, a second welcome, and anything else Apostle has no
        // business sending are ignored rather than fatal.

        _inbound.erase(_inbound.begin(),
                       _inbound.begin() +
                           static_cast<std::ptrdiff_t>(offset + size));
    }

    if (ended) {
        // A clean close is not an error once joined: the daemon may shut
        // down. Before the welcome it hung up on us, and that wants a
        // reason.
        if (_joined) {
            error.clear();
        } else {
            error = "apostle closed the connection before welcoming us";
        }
        return false;
    }
    return true;
}

void apostle_link::acknowledge(std::uint32_t sequence, long long render_ms) {
    // A failed write closes the socket, which the next read reports.
    std::string ignored;
    send(message_type::ack,
         "{\"sequence\":" + std::to_string(sequence) +
             ",\"renderMs\":" + std::to_string(render_ms) + "}",
         ignored);
}

void apostle_link::close() {
    if (_socket && _socket->is_open()) {
        std::string ignored;
        send(message_type::bye, "{\"reason\":\"closing\"}", ignored);
    }
    _socket.reset();
    _joined = false;
    _keepalive = default_keepalive_seconds;
    _client_id.clear();
    _inbound.clear();
}

//
// Send one whole message.
//
bool apostle_link::send(message_type type, std::string_view payload,
                        std::string &error) {
    std::vector<std::uint8_t> message;
    encode_message(type, payload, message);
    const std::size_t sent =
        _socket->write(std::as_bytes(std::span(message.data(), message.size())));
    if (sent != message.size()) {
        error = "write failed: " + describe(_socket->error());
        return false;
    }
    return true;
}

} // namespace disciple
