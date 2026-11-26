//
// The TCP broadcast listener and the Disciple registry.
//
// One thread and non-blocking sockets, like the ingest listener. A
// frame is around a hundred kilobytes, which does not leave a socket
// in one write, so every client carries an outbound buffer and asks
// for POLLOUT while it has bytes left. A reader on a slow link must
// not be able to stall the daemon for everyone else.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/broadcast_server.h"

#include "apostle/log.h"
#include "apostle/version.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
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

constexpr std::size_t read_chunk = 64 * 1024;

//
// Seconds since an arbitrary fixed point, unaffected by the wall
// clock being set.
//
long long now_seconds() {
    timespec when{};
    clock_gettime(CLOCK_MONOTONIC, &when);
    return when.tv_sec;
}

bool set_non_blocking(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

//
// The MIME type a captured frame carries, as a wire format.
//
frame_format format_of(const std::string &mime_type) {
    return mime_type == "image/jpeg" ? frame_format::jpeg
                                     : frame_format::png;
}

} // namespace

//
// One connected Disciple.
//
struct broadcast_server::client {
    explicit client(int fd, std::string name)
        : descriptor(fd)
        , id(std::move(name))
        , last_heard(now_seconds()) {
    }

    int descriptor;
    std::string id;

    // What the peer has sent that is not yet a whole message.
    std::vector<std::uint8_t> inbound;

    // What is still to go out, and how much of it has gone.
    std::vector<std::uint8_t> outbound;
    std::size_t sent = 0;

    // Set once hello has arrived. Frames go only to registered peers.
    bool registered = false;

    // What the peer said it accepts.
    bool takes_png = false;
    bool takes_jpeg = false;

    // Set when the peer accepts image/gray-packed and described a panel
    // that can be packed for. Such a peer is always sent its panel's
    // pixels, reduced here, never the encoded page.
    bool takes_packed = false;
    panel_spec panel;

    // The device, for the log.
    std::string device;

    // Flow control. A frame held back because the previous one is
    // still unacknowledged, replaced rather than queued.
    frame pending;
    bool has_pending = false;
    bool awaiting_ack = false;

    // Whether this peer has ever acknowledged anything. Acks are
    // optional in the protocol, so a peer that never sends one is
    // paced by its socket draining instead. Waiting forever for an
    // ack that is not coming would stop the stream after one frame.
    bool acks = false;

    long long last_heard = 0;
    bool ping_sent = false;
};

broadcast_server::broadcast_server() {
}

broadcast_server::~broadcast_server() {
    close();
}

bool broadcast_server::open(int port, std::string &error) {
    _listener = socket(AF_INET, SOCK_STREAM, 0);
    if (_listener < 0) {
        error = std::string("cannot create broadcast socket: ") +
                std::strerror(errno);
        return false;
    }

    const int reuse = 1;
    setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, &reuse,
               sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    // Every interface. The readers are on the LAN, unlike Evangelist.
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(_listener, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0) {
        error = "cannot bind *:" + std::to_string(port) + ": " +
                std::strerror(errno);
        ::close(_listener);
        _listener = -1;
        return false;
    }

    if (listen(_listener, 16) < 0 || !set_non_blocking(_listener)) {
        error = std::string("cannot listen on the broadcast port: ") +
                std::strerror(errno);
        ::close(_listener);
        _listener = -1;
        return false;
    }

    return true;
}

void broadcast_server::close() {
    for (auto &entry : _clients) {
        client &peer = *entry.second;
        // A reason costs one small write and turns a mystery
        // disconnection on the reader into a log line.
        queue(peer, message_type::bye,
              "{\"reason\":\"apostle is shutting down\"}");
        flush(peer);
        ::close(peer.descriptor);
    }
    _clients.clear();

    if (_listener >= 0) {
        ::close(_listener);
        _listener = -1;
    }
}

void broadcast_server::watch(std::vector<pollfd> &out) const {
    if (_listener >= 0) {
        out.push_back(pollfd{_listener, POLLIN, 0});
    }
    for (const auto &entry : _clients) {
        const client &peer = *entry.second;
        short events = POLLIN;
        if (peer.sent < peer.outbound.size()) {
            events |= POLLOUT;
        }
        out.push_back(pollfd{peer.descriptor, events, 0});
    }
}

void broadcast_server::ready(int descriptor, short revents) {
    if (descriptor < 0 || revents == 0) {
        return;
    }
    if (descriptor == _listener) {
        accept_all();
        return;
    }

    auto found = _clients.find(descriptor);
    if (found == _clients.end()) {
        return;
    }

    if ((revents & POLLOUT) != 0) {
        flush(*found->second);
    }

    // flush() can close the connection, so look again rather than
    // reusing an iterator that may now dangle.
    found = _clients.find(descriptor);
    if (found == _clients.end()) {
        return;
    }

    if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        receive(*found->second);
    }
}

void broadcast_server::newest_frame(
    std::function<const frame *()> source) {
    _newest = std::move(source);
}

int broadcast_server::client_count() const {
    int count = 0;
    for (const auto &entry : _clients) {
        if (entry.second->registered) {
            ++count;
        }
    }
    return count;
}

//
// Take every waiting connection.
//
void broadcast_server::accept_all() {
    for (;;) {
        sockaddr_in from{};
        socklen_t from_size = sizeof(from);
        const int peer = accept(_listener,
                                reinterpret_cast<sockaddr *>(&from),
                                &from_size);
        if (peer < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK &&
                errno != EINTR) {
                log_write(log_level::warn,
                          "broadcast accept failed: {}",
                          std::strerror(errno));
            }
            return;
        }

        if (!set_non_blocking(peer)) {
            ::close(peer);
            continue;
        }

        const int nodelay = 1;
        setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                   sizeof(nodelay));

        char address[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));

        char id[8];
        std::snprintf(id, sizeof(id), "c%03d", _next_id++);

        _clients.emplace(peer, std::make_unique<client>(peer, id));
        log_write(log_level::info, "disciple {} connected from {}", id,
                  address);
    }
}

//
// Read what is available and act on every whole message in it.
//
void broadcast_server::receive(client &peer) {
    std::uint8_t buffer[read_chunk];
    const int descriptor = peer.descriptor;

    for (;;) {
        const ssize_t got =
            recv(descriptor, buffer, sizeof(buffer), 0);
        if (got == 0) {
            drop(descriptor, "closed the connection");
            return;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            drop(descriptor, std::strerror(errno));
            return;
        }

        peer.inbound.insert(peer.inbound.end(), buffer,
                            buffer + static_cast<std::size_t>(got));
        peer.last_heard = now_seconds();
        peer.ping_sent = false;

        for (;;) {
            message_type type = message_type::ping;
            std::size_t offset = 0;
            std::size_t size = 0;
            std::string problem;

            const decode_result state = decode_message(
                peer.inbound.data(), peer.inbound.size(), type, offset,
                size, problem);

            if (state == decode_result::need_more) {
                break;
            }
            if (state == decode_result::failed) {
                log_write(log_level::warn, "disciple {}: {}", peer.id,
                          problem);
                drop(descriptor, "protocol error");
                return;
            }

            dispatch(peer, type, peer.inbound.data() + offset, size);
            if (_clients.find(descriptor) == _clients.end()) {
                return;
            }
            peer.inbound.erase(peer.inbound.begin(),
                               peer.inbound.begin() + offset + size);
        }
    }

    flush(peer);
}

//
// Act on one message.
//
void broadcast_server::dispatch(client &peer, message_type type,
                                const std::uint8_t *payload,
                                std::size_t size) {
    const std::string_view text(
        reinterpret_cast<const char *>(payload), size);

    switch (type) {
    case message_type::hello: {
        long long protocol = 0;
        if (!json_number(text, "protocol", protocol) ||
            protocol != protocol_version) {
            log_write(log_level::warn,
                      "disciple {} speaks protocol {}, this is {}",
                      peer.id, protocol, protocol_version);
            queue(peer, message_type::bye,
                  "{\"reason\":\"protocol mismatch\"}");
            flush(peer);
            drop(peer.descriptor, "protocol mismatch");
            return;
        }

        peer.takes_png = json_array_has(text, "accepts", "image/png");
        peer.takes_jpeg = json_array_has(text, "accepts", "image/jpeg");
        json_string(text, "device", peer.device);

        long long width = 0;
        long long height = 0;
        long long levels = 0;
        json_number(text, "width", width);
        json_number(text, "height", height);
        json_number(text, "grayLevels", levels);
        peer.panel.width = static_cast<int>(std::clamp(width, 0LL, 1LL << 20));
        peer.panel.height =
            static_cast<int>(std::clamp(height, 0LL, 1LL << 20));
        peer.panel.gray_levels =
            static_cast<int>(std::clamp(levels, 0LL, 1LL << 20));
        peer.takes_packed =
            json_array_has(text, "accepts", "image/gray-packed") &&
            peer.panel.valid();

        if (!peer.takes_png && !peer.takes_jpeg && !peer.takes_packed) {
            queue(peer, message_type::bye,
                  "{\"reason\":\"no format in common: this apostle sends "
                  "image/png, image/jpeg, and image/gray-packed for a "
                  "panel it can pack for\"}");
            flush(peer);
            drop(peer.descriptor, "no format in common");
            return;
        }

        peer.registered = true;
        queue(peer, message_type::welcome,
              std::string("{\"ok\":true,\"clientId\":") +
                  json_quoted(peer.id) + ",\"serverVersion\":" +
                  json_quoted(version) + ",\"keepalive\":" +
                  std::to_string(keepalive_seconds) + "}");

        log_write(log_level::info,
                  "disciple {} registered, device {}, accepts{}{}{}",
                  peer.id,
                  peer.device.empty() ? "unstated" : peer.device,
                  peer.takes_png ? " png" : "",
                  peer.takes_jpeg ? " jpeg" : "",
                  peer.takes_packed
                      ? " gray-packed " + std::to_string(peer.panel.width) +
                            "x" + std::to_string(peer.panel.height) + "/" +
                            std::to_string(peer.panel.gray_levels)
                      : std::string());

        // The current page, so the reader does not sit on a status
        // screen until the next one is turned.
        if (_newest) {
            if (const frame *current = _newest()) {
                send_frame(peer, *current);
            }
        }
        break;
    }

    case message_type::ack: {
        long long sequence = 0;
        long long render_ms = 0;
        json_number(text, "sequence", sequence);

        peer.acks = true;
        peer.awaiting_ack = false;

        if (json_number(text, "renderMs", render_ms)) {
            log_write(log_level::debug,
                      "disciple {} drew frame {} in {} ms", peer.id,
                      sequence, render_ms);
        }

        if (peer.has_pending) {
            const frame held = std::move(peer.pending);
            peer.has_pending = false;
            peer.pending = frame();
            send_frame(peer, held);
        }
        break;
    }

    case message_type::ping:
        queue(peer, message_type::pong, std::string_view());
        break;

    case message_type::pong:
        // last_heard was already refreshed by the read itself.
        break;

    case message_type::bye: {
        std::string reason;
        json_string(text, "reason", reason);
        log_write(log_level::info, "disciple {} said bye{}{}", peer.id,
                  reason.empty() ? "" : ": ", reason);
        drop(peer.descriptor, "said bye");
        break;
    }

    default:
        log_write(log_level::warn,
                  "disciple {} sent a {} message, which apostle does "
                  "not expect",
                  peer.id, message_type_name(type));
        break;
    }
}

void broadcast_server::publish(const frame &value) {
    for (auto &entry : _clients) {
        client &peer = *entry.second;
        if (!peer.registered) {
            continue;
        }

        // Hold it back while the previous frame is still in flight,
        // replacing whatever was held rather than queueing behind it.
        const bool busy = peer.awaiting_ack ||
                          peer.sent < peer.outbound.size();
        if (busy) {
            peer.pending = value;
            peer.has_pending = true;
            continue;
        }

        send_frame(peer, value);
    }
}

//
// Put one frame on the wire for one Disciple.
//
void broadcast_server::send_frame(client &peer, const frame &value) {
    frame_header header;
    header.sequence = static_cast<std::uint32_t>(value.sequence);
    const std::vector<std::uint8_t> *image = &value.bytes;

    if (peer.takes_packed) {
        image = reduced_for(value, peer.panel);
        if (image == nullptr) {
            return;
        }
        header.width = static_cast<std::uint16_t>(peer.panel.width);
        header.height = static_cast<std::uint16_t>(peer.panel.height);
        header.format = frame_format::gray_packed;
        header.gray_levels =
            static_cast<std::uint8_t>(peer.panel.gray_levels);
    } else {
        const frame_format format = format_of(value.mime_type);
        if ((format == frame_format::png && !peer.takes_png) ||
            (format == frame_format::jpeg && !peer.takes_jpeg)) {
            log_write(log_level::debug,
                      "disciple {} does not accept {}, frame {} skipped",
                      peer.id, value.mime_type, value.sequence);
            return;
        }
        header.width = static_cast<std::uint16_t>(value.width);
        header.height = static_cast<std::uint16_t>(value.height);
        header.format = format;
        header.gray_levels = 0;
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(frame_header_bytes + image->size());
    encode_frame_header(header, payload);
    payload.insert(payload.end(), image->begin(), image->end());

    encode_message(message_type::frame, payload.data(), payload.size(),
                   peer.outbound);

    // Only wait for an ack from a peer that has shown it sends them.
    peer.awaiting_ack = peer.acks;
    flush(peer);
}

//
// The frame reduced for one panel, from the cache when possible.
//
// Returns:
//      The packed pixels, or null when the page cannot be decoded.
//
// Notes:
//      A page is decoded once, and reduced once per distinct panel, so
//      a flock of identical readers costs one reduction however large
//      it is. The cache holds only the current page.
//
const std::vector<std::uint8_t> *
broadcast_server::reduced_for(const frame &value, const panel_spec &panel) {
    if (value.session != _cached_session ||
        value.sequence != _cached_sequence) {
        _cached_session = value.session;
        _cached_sequence = value.sequence;
        _reduced.clear();
        std::string problem;
        _decoded_ok = decode_to_grey(value.bytes, _decoded, problem);
        if (!_decoded_ok) {
            log_write(log_level::warn, "cannot decode frame {}: {}",
                      value.sequence, problem);
        }
    }
    if (!_decoded_ok) {
        return nullptr;
    }

    for (const auto &entry : _reduced) {
        if (entry.first == panel) {
            return &entry.second;
        }
    }
    _reduced.emplace_back(panel, reduce_to_panel(_decoded, panel));
    return &_reduced.back().second;
}

//
// Append a message to a client's outbound buffer.
//
void broadcast_server::queue(client &peer, message_type type,
                             std::string_view payload) {
    encode_message(type, payload, peer.outbound);
}

//
// Write as much of the outbound buffer as the socket will take.
//
void broadcast_server::flush(client &peer) {
    const int descriptor = peer.descriptor;

    while (peer.sent < peer.outbound.size()) {
        const ssize_t wrote =
            send(descriptor, peer.outbound.data() + peer.sent,
                 peer.outbound.size() - peer.sent, MSG_NOSIGNAL);
        if (wrote > 0) {
            peer.sent += static_cast<std::size_t>(wrote);
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // The socket is full. poll() will say when it drains.
            return;
        }
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        drop(descriptor, std::strerror(errno));
        return;
    }

    peer.outbound.clear();
    peer.sent = 0;

    // The wire is clear. A peer that does not acknowledge frames is
    // paced by exactly this: the next frame goes when the last one
    // has actually left.
    if (!peer.awaiting_ack && peer.has_pending) {
        const frame held = std::move(peer.pending);
        peer.has_pending = false;
        peer.pending = frame();
        send_frame(peer, held);
    }
}

void broadcast_server::tick() {
    const long long now = now_seconds();
    std::vector<int> silent;

    for (auto &entry : _clients) {
        client &peer = *entry.second;
        const long long quiet = now - peer.last_heard;

        if (!peer.ping_sent && quiet >= keepalive_seconds) {
            queue(peer, message_type::ping, std::string_view());
            flush(peer);
            peer.ping_sent = true;
            continue;
        }
        if (peer.ping_sent && quiet >= 2 * keepalive_seconds) {
            silent.push_back(peer.descriptor);
        }
    }

    for (const int descriptor : silent) {
        drop(descriptor, "stopped answering pings");
    }
}

void broadcast_server::drop(int descriptor, const char *why) {
    const auto found = _clients.find(descriptor);
    if (found == _clients.end()) {
        return;
    }

    log_write(log_level::info, "disciple {} gone: {}",
              found->second->id, why);
    ::close(descriptor);
    _clients.erase(found);
}

} // namespace apostle
