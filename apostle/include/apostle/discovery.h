//
// The discovery responder: how a reader with no configuration finds
// this daemon on the LAN.
//
// "Discovery" in docs/PROTOCOL.md. A Disciple that does not know where
// Apostle is broadcasts a `discover` datagram to the broadcast port
// over UDP, and this answers it directly with an `announce` naming the
// TCP port to join. Nothing is sent unless somebody asks, so an idle
// network carries no beacon traffic, and a reader that has just woken
// up is answered at once instead of waiting for the next beacon.
//
// The datagrams use the same TCST envelope as the TCP stream, one
// message per datagram, so the existing codec reads them unchanged.
//
// Like the two listeners, this owns its descriptor but not the loop.
// See main.cpp.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_DISCOVERY_H
#define APOSTLE_DISCOVERY_H

#include <cstdint>
#include <string>
#include <vector>

#include <poll.h>

namespace apostle {

//
// Answers discover datagrams with an announce.
//
class discovery_responder {
public:
    discovery_responder() = default;
    ~discovery_responder();

    discovery_responder(const discovery_responder &) = delete;
    discovery_responder &operator=(const discovery_responder &) = delete;

    //
    // Bind the UDP port on every interface.
    //
    // Parameters:
    //      udp_port    - Port to listen on, the same number as the
    //                    broadcast port.
    //      tcp_port    - Broadcast port to tell Disciples to join.
    //      error       - Receives a message when the bind fails.
    //
    // Returns:
    //      True when the socket is ready.
    //
    // Notes:
    //      Every interface, like the broadcast listener, because the
    //      readers asking are on the LAN. That includes loopback,
    //      which is what lets a test client on this same machine
    //      find the daemon with no network at all.
    //
    bool open(int udp_port, int tcp_port, std::string &error);

    //
    // Close the socket. Safe to call when it was never opened.
    //
    void close();

    //
    // Descriptors to watch, appended to out. Nothing when closed.
    //
    void watch(std::vector<pollfd> &out) const;

    //
    // Handle a descriptor poll() reported on.
    //
    // Parameters:
    //      descriptor  - The descriptor. One that is not this
    //                    responder's is ignored.
    //
    void ready(int descriptor);

private:
    int _socket = -1;

    // The whole announce datagram, envelope included.
    std::vector<std::uint8_t> _announce;
};

//
// The announce payload this daemon sends.
//
// Parameters:
//      tcp_port    - Broadcast port Disciples should connect to.
//      host        - This machine's name, for the reader's log.
//
// Returns:
//      JSON, exactly as docs/PROTOCOL.md specifies it.
//
std::string announce_payload(int tcp_port, const std::string &host);

} // namespace apostle

#endif // APOSTLE_DISCOVERY_H
