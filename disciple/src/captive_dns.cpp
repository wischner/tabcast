//
// The captive portal's DNS: every name is the reader.
//
// RFC 1035 message layout, the minimum of it. A question arrives as a
// twelve byte header and one question; the answer repeats both and
// adds one A record pointing back at the question's name. Nothing is
// forwarded anywhere, because on the reader's own setup network there
// is nowhere to forward to.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/portal.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>

namespace disciple {
namespace {

constexpr std::size_t header_bytes = 12;

// Record type and class numbers.
constexpr int type_a = 1;
constexpr int type_any = 255;
constexpr int class_in = 1;

// How long a phone may keep the answer. Short, so that once setup is
// over the phone stops believing every name is the reader.
constexpr std::uint32_t answer_ttl_seconds = 10;

std::uint16_t read16(const std::uint8_t *at) {
    return static_cast<std::uint16_t>((at[0] << 8) | at[1]);
}

void put16(std::vector<std::uint8_t> &out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

std::vector<std::uint8_t> dns_answer(const std::uint8_t *query,
                                     std::size_t size,
                                     std::uint32_t address) {
    if (size < header_bytes) {
        return {};
    }

    const std::uint16_t flags = read16(query + 2);
    const bool is_response = (flags & 0x8000) != 0;
    const unsigned opcode = (flags >> 11) & 0x0f;
    if (is_response || opcode != 0 || read16(query + 4) != 1) {
        return {};
    }

    // Walk the question's name, label by label. A question never uses
    // compression, so a pointer here is a malformed query.
    std::size_t at = header_bytes;
    for (;;) {
        if (at >= size) {
            return {};
        }
        const std::uint8_t length = query[at];
        if (length == 0) {
            ++at;
            break;
        }
        if ((length & 0xc0) != 0) {
            return {};
        }
        at += 1 + length;
    }
    if (at + 4 > size) {
        return {};
    }
    const unsigned type = read16(query + at);
    const unsigned klass = read16(query + at + 2);
    const std::size_t question_end = at + 4;

    const bool answerable =
        (type == type_a || type == type_any) && klass == class_in;

    std::vector<std::uint8_t> out;
    out.reserve(question_end + 16);

    out.push_back(query[0]);
    out.push_back(query[1]);
    // Response, authoritative, recursion desired copied, no error.
    put16(out, 0x8400 | (flags & 0x0100));
    put16(out, 1);
    put16(out, answerable ? 1 : 0);
    put16(out, 0);
    put16(out, 0);
    out.insert(out.end(), query + header_bytes, query + question_end);

    if (answerable) {
        // The name, as a pointer back to the question at offset 12.
        put16(out, 0xc000 | header_bytes);
        put16(out, type_a);
        put16(out, class_in);
        put16(out, answer_ttl_seconds >> 16);
        put16(out, answer_ttl_seconds & 0xffff);
        put16(out, 4);
        // The address is in network byte order, so its bytes in memory
        // are already the order the record wants.
        std::uint8_t bytes[4];
        std::memcpy(bytes, &address, sizeof(bytes));
        out.insert(out.end(), bytes, bytes + sizeof(bytes));
    }
    return out;
}

bool captive_dns::open(inkerface::network &net, int port,
                       const std::string &address, std::string &error) {
    close();

    // Parsed by hand: four numbers, kept as the four bytes in order,
    // which is network byte order.
    unsigned parts[4] = {0, 0, 0, 0};
    char tail = 0;
    if (std::sscanf(address.c_str(), "%u.%u.%u.%u%c", &parts[0], &parts[1],
                    &parts[2], &parts[3], &tail) != 4 ||
        parts[0] > 255 || parts[1] > 255 || parts[2] > 255 ||
        parts[3] > 255) {
        error = "not an IPv4 address: " + address;
        return false;
    }

    _socket.emplace(net, static_cast<std::uint16_t>(port));
    if (!*_socket) {
        error = "cannot answer DNS on port " + std::to_string(port);
        _socket.reset();
        return false;
    }
    const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(parts[0]),
                                   static_cast<std::uint8_t>(parts[1]),
                                   static_cast<std::uint8_t>(parts[2]),
                                   static_cast<std::uint8_t>(parts[3])};
    std::memcpy(&_address, bytes, sizeof(_address));
    return true;
}

bool captive_dns::is_open() const {
    return _socket.has_value();
}

void captive_dns::close() {
    _socket.reset();
}

void captive_dns::poll() {
    if (!_socket) {
        return;
    }
    std::array<std::byte, 512> question{};
    inkerface::datagram_source from;
    for (;;) {
        const std::size_t got =
            _socket->receive(question, from, std::chrono::milliseconds(0));
        if (got == 0) {
            return;
        }
        const std::vector<std::uint8_t> answer = dns_answer(
            reinterpret_cast<const std::uint8_t *>(question.data()), got,
            _address);
        if (!answer.empty()) {
            static_cast<void>(_socket->send_to(
                from.address, from.port,
                std::as_bytes(std::span(answer.data(), answer.size()))));
        }
    }
}

} // namespace disciple
