//
// The loopback ingest listener.
//
// Serves the two endpoints in docs/PROTOCOL.md section 1:
// POST /api/v1/frame, which Evangelist uploads captures to, and
// GET /api/v1/health, which it probes to light the status dot in the
// control bar. OPTIONS is answered for both so a curl or devtools
// test is not blocked by CORS.
//
// It binds 127.0.0.1 and only 127.0.0.1. v1 has no authentication, so
// binding anything else would hand frame injection to the whole
// network. That is a deliberate constraint of the protocol and not a
// detail to relax without adding a token first.
//
// The server owns its descriptors but not the loop. It reports what it
// wants watched and is handed descriptors back when they are ready, so
// the daemon can wait on the listener and on signals in one place. See
// main.cpp.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_HTTP_SERVER_H
#define APOSTLE_HTTP_SERVER_H

#include "apostle/frame_store.h"
#include "apostle/http.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <poll.h>

namespace apostle {

//
// Accepts connections on loopback and serves the ingest API.
//
class http_server {
public:
    //
    // Construct a server over a frame store.
    //
    // Parameters:
    //      frames      - Where accepted frames are put. Must outlive
    //                    the server.
    //
    explicit http_server(frame_store &frames);

    ~http_server();

    http_server(const http_server &) = delete;
    http_server &operator=(const http_server &) = delete;

    //
    // Bind and listen on the loopback address.
    //
    // Parameters:
    //      port        - TCP port, normally default_ingest_port.
    //      error       - Receives a message when the bind fails, with
    //                    the reason already appended.
    //
    // Returns:
    //      True when the socket is listening.
    //
    // Notes:
    //      A failure here is almost always a second daemon already
    //      holding the port, which is worth saying plainly rather
    //      than reporting as a bare errno.
    //
    bool open(int port, std::string &error);

    //
    // Close the listener and every open connection.
    //
    void close();

    //
    // Descriptors this server wants watched for readability.
    //
    // Parameters:
    //      out         - Receives the descriptors, appended.
    //
    void watch(std::vector<pollfd> &out) const;

    //
    // Handle a descriptor that poll() reported as ready.
    //
    // Parameters:
    //      descriptor  - The ready descriptor. One that does not
    //                    belong to this server is ignored, so the
    //                    caller can pass everything it saw.
    //
    void ready(int descriptor);

    //
    // Call a handler with every frame that is accepted.
    //
    // Parameters:
    //      handler     - Called after the frame has been stored. This
    //                    is how an accepted frame reaches the
    //                    broadcast listener without either half
    //                    knowing about the other.
    //
    void on_frame(std::function<void(const frame &)> handler);

    //
    // Ask something else how many Disciples are connected.
    //
    // Parameters:
    //      counter     - Called to fill in `clients` in the replies
    //                    to Evangelist. Without one the count is
    //                    reported as zero, which is the truthful
    //                    answer for a daemon with no broadcast half.
    //
    void client_counter(std::function<int()> counter);

private:
    struct connection;

    void accept_one();
    void service(connection &link);
    void respond(connection &link, int status, const std::string &json);
    void handle_request(connection &link);
    void handle_frame(connection &link);
    void handle_health(connection &link);
    void drop(int descriptor);

    int client_count() const;

    frame_store &_frames;
    std::function<void(const frame &)> _on_frame;
    std::function<int()> _client_counter;
    int _listener = -1;
    std::map<int, std::unique_ptr<connection>> _connections;
};

} // namespace apostle

#endif // APOSTLE_HTTP_SERVER_H
