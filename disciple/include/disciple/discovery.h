//
// Finding an Apostle on the LAN without being told where it is.
//
// "Discovery" in docs/PROTOCOL.md. The reader broadcasts a `discover`
// datagram to the broadcast port over UDP, with inkerface's udp_socket,
// and joins whichever Apostle answers first with an `announce`. A reader
// has no keyboard to type an address into and no reason to know one, so
// this is how it joins the flock when it is switched on.
//
// Only the asking and the answer are here. When to ask again, and what
// to do with the answer, is the reader's business.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_DISCOVERY_H
#define DISCIPLE_DISCOVERY_H

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include <optional>
#include <string>

namespace disciple {

//
// Where an announcing Apostle can be joined.
//
struct apostle_address {
    // The address the announce came from. Numeric, so connecting to it
    // needs no name lookup.
    std::string host;

    // Broadcast port from the announce.
    int port = 0;

    // The daemon's own name for its machine, for the log only.
    std::string name;
};

//
// One UDP socket that asks for Apostles and hears their answers.
//
class discovery {
public:
    //
    // Open the socket on any free port.
    //
    // Parameters:
    //      net         - The network, connected. Outlives the socket.
    //      error       - Receives why it could not be opened.
    //
    bool open(inkerface::network &net, std::string &error);

    //
    // Whether open() succeeded and close() has not been called since.
    //
    bool is_open() const;

    //
    // Broadcast a discover.
    //
    // Parameters:
    //      port        - UDP port to ask on, the broadcast port.
    //
    // Returns:
    //      True when it went out on at least one network.
    //
    // Notes:
    //      inkerface decides where a broadcast goes: the joined network
    //      on a reader; every interface and loopback on a desktop, so the
    //      emulation finds a daemon on the same machine.
    //
    bool ask(int port);

    //
    // Read the answers that have arrived, without waiting.
    //
    // Parameters:
    //      found       - Receives the first usable Apostle.
    //      note        - Receives a sentence about an answer that was
    //                    refused, such as a daemon speaking another
    //                    protocol revision. Empty otherwise.
    //
    // Returns:
    //      True when found was filled in.
    //
    bool read_answer(apostle_address &found, std::string &note);

    //
    // Close the socket. Safe when it is not open.
    //
    void close();

private:
    std::optional<inkerface::udp_socket> _socket;
};

//
// The discover payload describing this build.
//
// Returns:
//      JSON, exactly as docs/PROTOCOL.md specifies it.
//
std::string discover_payload();

} // namespace disciple

#endif // DISCIPLE_DISCOVERY_H
