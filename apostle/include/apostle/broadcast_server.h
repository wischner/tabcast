//
// The TCP broadcast listener and the Disciple registry.
//
// Section 2 of docs/PROTOCOL.md. Disciples connect, say hello, and
// stay connected; Apostle pushes frames and they never poll. This
// listener binds every interface, unlike the ingest endpoint, because
// the readers are on the LAN and the daemon is not.
//
// v1 has no pairing handshake: any host that can reach the port and
// speak hello joins the flock. That is acceptable on a trusted network
// and nowhere else, and the protocol document says so in the same
// words.
//
// Like the ingest listener, this owns its descriptors but not the
// loop. See main.cpp.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_BROADCAST_SERVER_H
#define APOSTLE_BROADCAST_SERVER_H

#include "apostle/frame_store.h"
#include "apostle/wire.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <poll.h>

namespace apostle {

//
// Seconds of silence before a ping is sent, and again before the
// connection is given up on. Reported to each Disciple in welcome.
//
inline constexpr int keepalive_seconds = 30;

//
// Accepts Disciples and pushes frames to them.
//
class broadcast_server {
public:
    broadcast_server();
    ~broadcast_server();

    broadcast_server(const broadcast_server &) = delete;
    broadcast_server &operator=(const broadcast_server &) = delete;

    //
    // Bind and listen on every interface.
    //
    // Parameters:
    //      port        - TCP port, normally default_broadcast_port.
    //      error       - Receives a message when the bind fails.
    //
    // Returns:
    //      True when the socket is listening.
    //
    bool open(int port, std::string &error);

    //
    // Say bye to every Disciple and close everything.
    //
    void close();

    //
    // Descriptors this server wants watched, and for what.
    //
    // Parameters:
    //      out         - Receives the descriptors, appended. A client
    //                    with bytes still to write asks for POLLOUT
    //                    as well, which is what keeps a slow reader
    //                    from blocking the daemon.
    //
    void watch(std::vector<pollfd> &out) const;

    //
    // Handle a descriptor that poll() reported on.
    //
    // Parameters:
    //      descriptor  - The descriptor. One that does not belong to
    //                    this server is ignored.
    //      revents     - What poll() reported.
    //
    void ready(int descriptor, short revents);

    //
    // Send a frame to every registered Disciple.
    //
    // Parameters:
    //      value       - The frame, as accepted from Evangelist.
    //
    // Notes:
    //      At most one frame is outstanding per Disciple. A frame
    //      arriving while the previous one is unacknowledged replaces
    //      it rather than queueing behind it, so a reader that falls
    //      behind shows the newest page and never works through a
    //      backlog of stale ones.
    //
    void publish(const frame &value);

    //
    // Housekeeping that depends on the clock rather than on a
    // descriptor: keepalive pings and dropping silent connections.
    //
    // Notes:
    //      Call it regularly. The daemon polls with a timeout so that
    //      it is called even when nothing at all is happening, which
    //      is exactly when a dead connection needs noticing.
    //
    void tick();

    //
    // How many Disciples are registered and being sent frames.
    //
    int client_count() const;

private:
    struct client;

    void accept_all();
    void receive(client &peer);
    void flush(client &peer);
    void dispatch(client &peer, message_type type,
                  const std::uint8_t *payload, std::size_t size);
    void send_frame(client &peer, const frame &value);
    void queue(client &peer, message_type type, std::string_view payload);
    void drop(int descriptor, const char *why);

    int _listener = -1;
    int _next_id = 1;
    std::map<int, std::unique_ptr<client>> _clients;
};

} // namespace apostle

#endif // APOSTLE_BROADCAST_SERVER_H
