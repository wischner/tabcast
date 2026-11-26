//
// The discovery responder.
//
// One non-blocking UDP socket. Every readable datagram is checked to
// be a well formed `discover` and answered with an `announce` sent
// back to whoever asked. Anything else is dropped without a reply,
// so a stray packet on the port cannot make the daemon talk.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/discovery.h"

#include "apostle/log.h"
#include "apostle/version.h"
#include "apostle/wire.h"

#include <cerrno>
#include <cstring>
#include <string_view>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace apostle {
namespace {

// A discover is a few dozen bytes. Anything bigger than this is not
// one, and is read only so it can be thrown away.
constexpr std::size_t datagram_limit = 2048;

//
// This machine's name, or a placeholder when it has none.
//
std::string host_name() {
    char name[256] = {0};
    if (gethostname(name, sizeof(name) - 1) != 0 || name[0] == '\0') {
        return "apostle";
    }
    return name;
}

} // namespace

std::string announce_payload(int tcp_port, const std::string &host) {
    return std::string("{\"name\":\"apostle\",\"version\":") +
           json_quoted(version) + ",\"protocol\":" +
           std::to_string(protocol_version) +
           ",\"port\":" + std::to_string(tcp_port) +
           ",\"host\":" + json_quoted(host) + "}";
}

discovery_responder::~discovery_responder() {
    close();
}

bool discovery_responder::open(int udp_port, int tcp_port,
                               std::string &error) {
    _socket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                     0);
    if (_socket < 0) {
        error = std::string("cannot create discovery socket: ") +
                std::strerror(errno);
        return false;
    }

    const int reuse = 1;
    setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(udp_port));
    // INADDR_ANY is also what receives broadcasts. A socket bound to
    // one interface's own address would not see them on Linux.
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(_socket, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0) {
        error = "cannot bind udp/*:" + std::to_string(udp_port) + ": " +
                std::strerror(errno);
        close();
        return false;
    }

    // The answer never changes while the daemon runs, so it is built
    // once rather than per request.
    _announce.clear();
    encode_message(message_type::announce,
                   announce_payload(tcp_port, host_name()), _announce);
    return true;
}

void discovery_responder::close() {
    if (_socket >= 0) {
        ::close(_socket);
        _socket = -1;
    }
}

void discovery_responder::watch(std::vector<pollfd> &out) const {
    if (_socket >= 0) {
        out.push_back(pollfd{_socket, POLLIN, 0});
    }
}

void discovery_responder::ready(int descriptor) {
    if (descriptor < 0 || descriptor != _socket) {
        return;
    }

    for (;;) {
        std::uint8_t buffer[datagram_limit];
        sockaddr_in from{};
        socklen_t from_size = sizeof(from);

        const ssize_t got =
            recvfrom(_socket, buffer, sizeof(buffer), 0,
                     reinterpret_cast<sockaddr *>(&from), &from_size);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            // EAGAIN is the normal way out: everything is read.
            return;
        }

        message_type type = message_type::ping;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::string problem;

        // One datagram is one whole message. need_more means it was
        // truncated or lied about its length, and is dropped like any
        // other malformed packet.
        const decode_result state =
            decode_message(buffer, static_cast<std::size_t>(got), type,
                           offset, size, problem);
        if (state != decode_result::done ||
            type != message_type::discover) {
            continue;
        }

        char address[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));

        const ssize_t sent =
            sendto(_socket, _announce.data(), _announce.size(), 0,
                   reinterpret_cast<sockaddr *>(&from), from_size);
        if (sent < 0) {
            log_write(log_level::warn,
                      "cannot answer discovery from {}: {}", address,
                      std::strerror(errno));
            continue;
        }

        const std::string_view text(
            reinterpret_cast<const char *>(buffer + offset), size);
        std::string device;
        json_string(text, "device", device);
        log_write(log_level::debug, "announced to {} at {}",
                  device.empty() ? "a disciple" : device, address);
    }
}

} // namespace apostle
