//
// The reader's connection to Apostle, over inkerface's tcp_socket.
//
// Section 2 of docs/PROTOCOL.md. The reader connects, says hello with
// its panel's size, and is sent the panel's own pixels from then on.
// Every read waits zero milliseconds, so the main loop that polls this
// never stalls on it; connecting is the one call that blocks, for at
// most a few seconds, while the panel already says "Connecting...".
//
// The link is deliberately not in charge of anything. It hands whole
// frames to the caller, answers pings, and handles bye.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_LINK_H
#define DISCIPLE_LINK_H

#include "disciple/wire.h"

#include <inkerface/network.h>
#include <inkerface/socket.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace disciple {

//
// Seconds of silence after which Apostle pings, when its welcome does
// not say. The value docs/PROTOCOL.md gives as the default.
//
inline constexpr int default_keepalive_seconds = 30;

//
// What the hello tells Apostle about this reader.
//
struct reader_identity {
    // The reader's own name, sheep_ and eight hex digits.
    std::string name;
    // The panel's size in pixels.
    int width = 0;
    int height = 0;
};

//
// Called with each frame as it completes. The image is borrowed from the
// link's buffer and is valid only during the call: draw it, do not keep
// it. That is what spares a reader a second page-sized buffer.
//
using frame_handler =
    std::function<void(const frame_header &, std::span<const std::uint8_t>)>;

//
// A connection to an Apostle.
//
class apostle_link {
public:
    //
    // Set aside the receive buffer, once, for the largest message.
    //
    // Parameters:
    //      bytes       - Room for one whole frame message, envelope and
    //                    header included.
    //
    // Notes:
    //      Call it at start-up, before Wi-Fi fragments the heap. A reader
    //      has a few hundred kilobytes in all, and a page-sized buffer
    //      grown by doubling while the page arrives needs two or three
    //      times the page at once. Reserved here, the buffer never grows
    //      and is kept across reconnects.
    //
    void reserve(std::size_t bytes);

    //
    // Connect and say hello.
    //
    // Parameters:
    //      net         - The network, connected. Outlives the link.
    //      host        - Address or name to connect to.
    //      port        - Broadcast port.
    //      who         - What to say about this reader.
    //      error       - Receives why it failed.
    //
    // Returns:
    //      True when connected and the hello is sent. The welcome
    //      arrives through read_ready().
    //
    // Notes:
    //      Blocks for at most connect_timeout while connecting.
    //
    bool open(inkerface::network &net, const std::string &host, int port,
              const reader_identity &who, std::string &error);

    //
    // Whether a connection is open.
    //
    bool is_open() const;

    //
    // Whether Apostle has welcomed this reader.
    //
    bool joined() const;

    //
    // The keepalive interval from the welcome, in seconds.
    //
    int keepalive_seconds() const;

    //
    // The client id Apostle assigned, from the welcome.
    //
    const std::string &client_id() const;

    //
    // Where this link goes, as host:port.
    //
    const std::string &peer() const;

    //
    // Read whatever has arrived and pull out any complete frames.
    //
    // Parameters:
    //      on_frame    - Called with every frame that completed, in
    //                    order, before this returns.
    //      heard       - Set when any byte arrived, which is what proves
    //                    Apostle is still there.
    //      error       - Receives why the link ended. Left empty when
    //                    Apostle closed it cleanly.
    //
    // Returns:
    //      True while the link is still usable.
    //
    bool read_ready(const frame_handler &on_frame, bool &heard,
                    std::string &error);

    //
    // Tell Apostle a frame has been drawn, and how long that took, which
    // lets it pace itself to a slow panel.
    //
    void acknowledge(std::uint32_t sequence, long long render_ms);

    //
    // Say bye, when there is anyone to say it to, and close. Safe when
    // closed.
    //
    void close();

private:
    bool send(message_type type, std::string_view payload,
              std::string &error);
    void take_welcome(std::string_view text);

    std::optional<inkerface::tcp_socket> _socket;
    bool _joined = false;
    int _keepalive = default_keepalive_seconds;
    std::string _client_id;
    std::string _peer;
    std::vector<std::uint8_t> _inbound;
};

//
// The hello payload describing this reader and its panel.
//
// Returns:
//      JSON, exactly as docs/PROTOCOL.md specifies it. The reader always
//      asks for image/gray-packed at reader_gray_levels.
//
std::string hello_payload(const reader_identity &who);

} // namespace disciple

#endif // DISCIPLE_LINK_H
