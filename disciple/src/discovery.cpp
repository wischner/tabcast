//
// Finding an Apostle on the LAN.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/discovery.h"

#include "disciple/version.h"
#include "disciple/wire.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace disciple {
namespace {

using namespace std::chrono_literals;

// An announce is around a hundred bytes. Anything larger is not one.
constexpr std::size_t datagram_limit = 512;

} // namespace

std::string discover_payload() {
    return std::string("{\"name\":\"disciple\",\"version\":") +
           json_quoted(version) + ",\"protocol\":" +
           std::to_string(protocol_version) + "}";
}

bool discovery::open(inkerface::network &net, std::string &error) {
    close();
    _socket.emplace(net);
    if (!*_socket) {
        error = "cannot open a discovery socket";
        _socket.reset();
        return false;
    }
    return true;
}

bool discovery::is_open() const {
    return _socket.has_value();
}

bool discovery::ask(int port) {
    std::vector<std::uint8_t> message;
    encode_message(message_type::discover, discover_payload(), message);
    return _socket->broadcast(
        static_cast<std::uint16_t>(port),
        std::as_bytes(std::span(message.data(), message.size())));
}

bool discovery::read_answer(apostle_address &found, std::string &note) {
    note.clear();

    std::array<std::byte, datagram_limit> buffer{};
    inkerface::datagram_source from;
    for (;;) {
        const std::size_t got = _socket->receive(buffer, from, 0ms);
        if (got == 0) {
            return false;
        }
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(buffer.data());

        // One datagram is one whole message; anything else is dropped.
        message_type type = message_type::ping;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::string problem;
        if (decode_message(bytes, got, type, offset, size, problem) !=
                decode_result::done ||
            type != message_type::announce) {
            continue;
        }
        const std::string_view text(
            reinterpret_cast<const char *>(bytes + offset), size);

        long long protocol = 0;
        if (!json_number(text, "protocol", protocol) ||
            protocol != protocol_version) {
            note = "apostle at " + from.address + " speaks protocol " +
                   std::to_string(protocol) + ", this is " +
                   std::to_string(protocol_version);
            continue;
        }
        long long port = 0;
        if (!json_number(text, "port", port) || port < 1 || port > 65535) {
            note = "apostle at " + from.address + " announced no usable port";
            continue;
        }

        found.host = from.address;
        found.port = static_cast<int>(port);
        found.name.clear();
        json_string(text, "host", found.name);
        return true;
    }
}

void discovery::close() {
    _socket.reset();
}

} // namespace disciple
