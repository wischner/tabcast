//
// The newest captured frame, and nothing older.
//
// Apostle keeps exactly one frame: the last one Evangelist posted. A
// queue would be wrong here. A reader that falls behind should show
// the newest page rather than work through a backlog of stale ones,
// which is the same rule docs/PROTOCOL.md states for per-client flow
// control.
//
// Everything in the daemon runs on one thread around a single poll
// loop, so this needs no locking. If that ever stops being true, it
// stops being true here first.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_FRAME_STORE_H
#define APOSTLE_FRAME_STORE_H

#include <cstdint>
#include <string>
#include <vector>

namespace apostle {

//
// One captured frame and what Evangelist said about it.
//
struct frame {
    // The encoded image exactly as it arrived, not re-encoded.
    std::vector<std::uint8_t> bytes;

    // "image/png" or "image/jpeg".
    std::string mime_type;

    // Frame counter, starting at 1 within a session.
    long long sequence = 0;

    // Opaque id, stable for one broadcast.
    std::string session;

    // Captured pixel size, as reported by the extension.
    int width = 0;
    int height = 0;

    // Page title and URL, already percent-decoded. Either may be
    // empty, both are optional on the wire.
    std::string title;
    std::string url;
};

//
// Holds the newest frame.
//
class frame_store {
public:
    //
    // Replace the stored frame.
    //
    // Parameters:
    //      value       - The new frame, moved from.
    //
    void put(frame value);

    //
    // The stored frame.
    //
    // Returns:
    //      Pointer to the frame, or null when nothing has been posted
    //      yet. Valid until the next put().
    //
    const frame *newest() const;

    //
    // How many frames have been accepted since the daemon started.
    //
    // Notes:
    //      Counts accepted posts, not distinct pictures. Evangelist
    //      already drops a frame identical to the one before it, so
    //      this is a count of what actually travelled.
    //
    long long accepted() const;

private:
    frame _newest;
    bool _have = false;
    long long _accepted = 0;
};

} // namespace apostle

#endif // APOSTLE_FRAME_STORE_H
