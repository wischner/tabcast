//
// The setup portal's network side: a very small web server, and the
// DNS answers that make a phone open it by itself.
//
// While the reader is its own access point, a phone that joins it has
// no internet. Phones check for that by fetching a known page, and
// when the answer is not the one they expect they show a "sign in to
// network" sheet with whatever page came back instead. So every name
// is answered with the reader's own address, and every request for a
// page the reader does not serve is sent to the setup page. That is
// the whole trick behind every hotel Wi-Fi login, and it means nobody
// has to type an address.
//
// The server speaks just enough HTTP/1.1 for one phone filling in one
// form: one request per connection, a body only for POST, and a hard
// limit on size. It is not a general web server and must never face
// anything but the reader's own setup network.
//
// Both are built on inkerface's tcp_listener and udp_socket, and both
// are polled from the reader's main loop with zero waits: poll() takes
// whatever has arrived and returns at once.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_PORTAL_H
#define DISCIPLE_PORTAL_H

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace disciple {

//
// One request, as much of it as the portal looks at.
//
struct http_request {
    std::string method;
    // The path, without the query.
    std::string path;
    // Everything after '?', still encoded.
    std::string query;
    // The Host header, which tells a captive check from a real visit.
    std::string host;
    std::string body;
};

//
// One reply.
//
struct http_reply {
    int status = 200;
    std::string content_type = "text/html; charset=utf-8";
    // Sent as a Location header when not empty.
    std::string location;
    std::string body;
};

//
// A web server for one form.
//
class http_portal {
public:
    using handler = std::function<http_reply(const http_request &)>;

    //
    // Listen on every interface and serve through a handler.
    //
    // Parameters:
    //      net         - The network, connected or hosting. Outlives the
    //                    portal.
    //      port        - TCP port, 80 on a reader.
    //      on_request  - Turns each complete request into a reply.
    //      error       - Receives why it could not listen.
    //
    // Returns:
    //      True when listening.
    //
    bool open(inkerface::network &net, int port, handler on_request,
              std::string &error);

    //
    // Whether open() succeeded and close() has not been called since.
    //
    bool is_open() const;

    //
    // Take new connections, read requests, and answer the whole ones.
    //
    // Notes:
    //      Never waits. The handler may close the portal from inside.
    //
    void poll();

    //
    // Drop every connection and stop listening.
    //
    void close();

private:
    struct client {
        inkerface::tcp_socket link;
        std::string inbound;
        std::chrono::steady_clock::time_point last_heard;
    };

    void accept_all();
    bool serve(client &peer);

    std::optional<inkerface::tcp_listener> _listener;
    handler _handler;
    std::vector<client> _clients;
};

//
// Answers every DNS question for an A record with one address.
//
class captive_dns {
public:
    //
    // Listen for DNS questions.
    //
    // Parameters:
    //      net         - The network, hosting. Outlives the responder.
    //      port        - UDP port, 53 on a reader.
    //      address     - IPv4 address every name resolves to.
    //      error       - Receives why it could not listen.
    //
    bool open(inkerface::network &net, int port, const std::string &address,
              std::string &error);

    bool is_open() const;

    //
    // Answer every question that has arrived. Never waits.
    //
    void poll();

    void close();

private:
    std::optional<inkerface::udp_socket> _socket;
    std::uint32_t _address = 0;
};

//
// Build the answer to one DNS question.
//
// Parameters:
//      query, size - The question datagram.
//      address     - IPv4 address to answer with, network byte order.
//
// Returns:
//      The answer datagram, or nothing for a datagram that is not a
//      single standard question, which is dropped rather than
//      answered. A question for anything but an A record is answered
//      with no records, so the phone stops asking rather than waits.
//
std::vector<std::uint8_t> dns_answer(const std::uint8_t *query,
                                     std::size_t size,
                                     std::uint32_t address);

//
// Read a field from an application/x-www-form-urlencoded body.
//
// Returns:
//      The decoded value, or an empty string when the field is absent.
//
std::string form_value(std::string_view body, std::string_view key);

//
// Escape text for use inside HTML, attribute values included.
//
std::string html_escape(std::string_view text);

} // namespace disciple

#endif // DISCIPLE_PORTAL_H
