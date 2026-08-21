//
// Disciple's half of the broadcast connection.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/net.h"

#include "disciple/version.h"

#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace disciple {
namespace {

// How long to wait for the welcome before giving up. A daemon on the
// LAN answers immediately; anything slower than this is a daemon that
// is not going to answer at all.
constexpr int handshake_timeout_seconds = 5;

constexpr std::size_t read_chunk = 64 * 1024;

//
// Put a descriptor into or out of non-blocking mode.
//
bool set_non_blocking(int descriptor, bool on) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int wanted =
        on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(descriptor, F_SETFL, wanted) == 0;
}

} // namespace

std::string hello_payload() {
    return std::string("{\"name\":\"disciple\",\"version\":") +
           json_quoted(version) + ",\"protocol\":" +
           std::to_string(protocol_version) + ",\"device\":" +
           json_quoted(device_name) +
           ",\"width\":" + std::to_string(panel_width) +
           ",\"height\":" + std::to_string(panel_height) +
           ",\"grayLevels\":" + std::to_string(panel_gray_levels) +
           ",\"accepts\":[\"image/png\",\"image/jpeg\"]}";
}

connection::~connection() {
    close();
}

bool connection::open(const std::string &host, int port,
                      std::string &error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *found = nullptr;
    const std::string service = std::to_string(port);
    const int status =
        getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
    if (status != 0) {
        error = "cannot resolve " + host + ": " + gai_strerror(status);
        return false;
    }

    for (addrinfo *entry = found; entry != nullptr;
         entry = entry->ai_next) {
        const int candidate = socket(entry->ai_family,
                                     entry->ai_socktype,
                                     entry->ai_protocol);
        if (candidate < 0) {
            continue;
        }
        if (connect(candidate, entry->ai_addr, entry->ai_addrlen) == 0) {
            _socket = candidate;
            break;
        }
        ::close(candidate);
    }

    freeaddrinfo(found);

    if (_socket < 0) {
        error = "cannot reach apostle at " + host + ":" + service +
                ": " + std::strerror(errno);
        return false;
    }

    const int nodelay = 1;
    setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &nodelay,
               sizeof(nodelay));
    return true;
}

bool connection::handshake(std::string &error) {
    timeval timeout{};
    timeout.tv_sec = handshake_timeout_seconds;
    setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));

    std::vector<std::uint8_t> hello;
    encode_message(message_type::hello, hello_payload(), hello);
    if (!send_all(hello, error)) {
        return false;
    }

    // Read until the welcome is whole. Anything else arriving first
    // would be a daemon that does not follow its own protocol.
    for (;;) {
        message_type type = message_type::ping;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::string problem;

        const decode_result state =
            decode_message(_inbound.data(), _inbound.size(), type,
                           offset, size, problem);

        if (state == decode_result::failed) {
            error = problem;
            return false;
        }
        if (state == decode_result::done) {
            const std::string_view text(
                reinterpret_cast<const char *>(_inbound.data() + offset),
                size);

            if (type == message_type::bye) {
                std::string reason;
                json_string(text, "reason", reason);
                error = reason.empty() ? "apostle refused the connection"
                                       : "apostle refused us: " + reason;
                return false;
            }
            if (type != message_type::welcome) {
                error = std::string("expected welcome, got ") +
                        message_type_name(type);
                return false;
            }

            json_string(text, "clientId", _client_id);
            _inbound.erase(_inbound.begin(),
                           _inbound.begin() + offset + size);
            break;
        }

        std::uint8_t buffer[read_chunk];
        const ssize_t got = recv(_socket, buffer, sizeof(buffer), 0);
        if (got == 0) {
            error = "apostle closed the connection during the handshake";
            return false;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = (errno == EAGAIN || errno == EWOULDBLOCK)
                        ? "apostle did not answer the hello"
                        : std::string("read failed: ") +
                              std::strerror(errno);
            return false;
        }
        _inbound.insert(_inbound.end(), buffer,
                        buffer + static_cast<std::size_t>(got));
    }

    // From here the event loop owns the socket, so it must never
    // block in a read.
    timeout.tv_sec = 0;
    setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));
    if (!set_non_blocking(_socket, true)) {
        error = std::string("cannot set the socket non-blocking: ") +
                std::strerror(errno);
        return false;
    }
    return true;
}

const std::string &connection::client_id() const {
    return _client_id;
}

int connection::descriptor() const {
    return _socket;
}

bool connection::read_ready(std::vector<received_frame> &out,
                            std::string &error) {
    for (;;) {
        std::uint8_t buffer[read_chunk];
        const ssize_t got = recv(_socket, buffer, sizeof(buffer), 0);

        if (got == 0) {
            // A clean close. Not an error: the daemon is allowed to
            // shut down, and the caller says so rather than failing.
            error.clear();
            return false;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            error = std::string("read failed: ") + std::strerror(errno);
            return false;
        }

        _inbound.insert(_inbound.end(), buffer,
                        buffer + static_cast<std::size_t>(got));
    }

    for (;;) {
        message_type type = message_type::ping;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::string problem;

        const decode_result state =
            decode_message(_inbound.data(), _inbound.size(), type,
                           offset, size, problem);

        if (state == decode_result::need_more) {
            return true;
        }
        if (state == decode_result::failed) {
            error = problem;
            return false;
        }

        const std::uint8_t *payload = _inbound.data() + offset;

        switch (type) {
        case message_type::frame: {
            received_frame received;
            if (!decode_frame_header(payload, size, received.header)) {
                error = "a frame arrived with a malformed header";
                return false;
            }
            received.image.assign(payload + frame_header_bytes,
                                  payload + size);
            out.push_back(std::move(received));
            break;
        }

        case message_type::ping: {
            std::vector<std::uint8_t> pong;
            encode_message(message_type::pong, nullptr, 0, pong);
            std::string ignored;
            send_all(pong, ignored);
            break;
        }

        case message_type::bye: {
            const std::string_view text(
                reinterpret_cast<const char *>(payload), size);
            std::string reason;
            json_string(text, "reason", reason);
            error = reason.empty() ? std::string() : "apostle: " + reason;
            return false;
        }

        default:
            // pong, and anything else Apostle has no business
            // sending. Ignored rather than fatal: an unknown message
            // inside a valid envelope is what a newer daemon looks
            // like, and the envelope is what has to be understood.
            break;
        }

        _inbound.erase(_inbound.begin(),
                       _inbound.begin() + offset + size);
    }
}

void connection::acknowledge(std::uint32_t sequence,
                             long long render_ms) {
    const std::string payload =
        "{\"sequence\":" + std::to_string(sequence) +
        ",\"renderMs\":" + std::to_string(render_ms) + "}";

    std::vector<std::uint8_t> message;
    encode_message(message_type::ack, payload, message);

    std::string ignored;
    send_all(message, ignored);
}

void connection::close() {
    if (_socket < 0) {
        return;
    }

    std::vector<std::uint8_t> message;
    encode_message(message_type::bye, "{\"reason\":\"closing\"}",
                   message);
    std::string ignored;
    send_all(message, ignored);

    ::close(_socket);
    _socket = -1;
}

//
// Write a whole buffer, retrying around a socket that is full.
//
bool connection::send_all(const std::vector<std::uint8_t> &bytes,
                          std::string &error) {
    std::size_t sent = 0;

    while (sent < bytes.size()) {
        const ssize_t wrote = send(_socket, bytes.data() + sent,
                                   bytes.size() - sent, MSG_NOSIGNAL);
        if (wrote > 0) {
            sent += static_cast<std::size_t>(wrote);
            continue;
        }
        if (wrote < 0 && (errno == EINTR || errno == EAGAIN ||
                          errno == EWOULDBLOCK)) {
            // Everything written here is small: a hello, an ack, a
            // pong. Spinning briefly on a full socket buffer is
            // cheaper than carrying an outbound queue for them.
            continue;
        }
        error = std::string("write failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

} // namespace disciple
