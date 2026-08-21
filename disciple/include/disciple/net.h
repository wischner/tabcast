//
// Disciple's half of the broadcast connection.
//
// Connects to an Apostle, says hello, and then reads frames until the
// daemon or the user stops it. Section 2 of docs/PROTOCOL.md.
//
// The socket is deliberately not in charge of anything. It hands whole
// frames to the caller and the caller decides what to do with them,
// which is what lets the display backend own the event loop while this
// still gets its bytes. See backend::watch().
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_NET_H
#define DISCIPLE_NET_H

#include "disciple/wire.h"

#include <cstdint>
#include <string>
#include <vector>

namespace disciple {

//
// One frame as it came off the wire.
//
struct received_frame {
    frame_header header;
    std::vector<std::uint8_t> image;
};

//
// A connection to an Apostle.
//
class connection {
public:
    connection() = default;
    ~connection();

    connection(const connection &) = delete;
    connection &operator=(const connection &) = delete;

    //
    // Connect to a daemon.
    //
    // Parameters:
    //      host        - Address or name to connect to.
    //      port        - Broadcast port, normally
    //                    default_broadcast_port.
    //      error       - Receives a message when connecting fails.
    //
    // Returns:
    //      True when the socket is connected. The handshake has not
    //      happened yet.
    //
    bool open(const std::string &host, int port, std::string &error);

    //
    // Say hello and wait for the welcome.
    //
    // Parameters:
    //      error       - Receives a message when the exchange fails
    //                    or the daemon refuses this device.
    //
    // Returns:
    //      True when Apostle accepted this Disciple.
    //
    // Notes:
    //      This one exchange is done with a receive timeout rather
    //      than in the event loop. There is nothing to display until
    //      it succeeds, so waiting here keeps every later read simple
    //      and non-blocking.
    //
    bool handshake(std::string &error);

    //
    // The client id Apostle assigned, from the welcome.
    //
    const std::string &client_id() const;

    //
    // The socket, for whoever owns the event loop.
    //
    int descriptor() const;

    //
    // Read whatever has arrived and pull out any complete frames.
    //
    // Parameters:
    //      out         - Receives every frame that completed, in
    //                    order. Usually none or one.
    //      error       - Receives a message when the connection
    //                    failed. Left empty when the daemon closed
    //                    the connection cleanly.
    //
    // Returns:
    //      True while the connection is still usable.
    //
    // Notes:
    //      Answers ping with pong and handles bye itself. The caller
    //      only ever sees frames.
    //
    bool read_ready(std::vector<received_frame> &out,
                    std::string &error);

    //
    // Tell Apostle a frame has been drawn.
    //
    // Parameters:
    //      sequence    - The frame that was drawn.
    //      render_ms   - How long drawing it took.
    //
    // Notes:
    //      Optional in the protocol, and worth sending: it is what
    //      lets Apostle pace itself to a slow panel instead of
    //      pushing frames faster than they can be shown.
    //
    void acknowledge(std::uint32_t sequence, long long render_ms);

    //
    // Say bye and close.
    //
    void close();

private:
    bool send_all(const std::vector<std::uint8_t> &bytes,
                  std::string &error);

    int _socket = -1;
    std::string _client_id;
    std::vector<std::uint8_t> _inbound;
};

//
// The hello payload describing this build and its panel.
//
// Returns:
//      JSON, exactly as docs/PROTOCOL.md specifies it.
//
// Notes:
//      Built from the constants in version.h, so a change to the
//      panel geometry cannot leave the daemon believing something
//      else about this device.
//
std::string hello_payload();

} // namespace disciple

#endif // DISCIPLE_NET_H
