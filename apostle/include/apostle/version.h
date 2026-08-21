//
// Apostle version information.
//
// A single place for the daemon's version, reported on the command
// line and in the /api/v1/health reply so an Evangelist or a Disciple
// can tell which build it is talking to.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_VERSION_H
#define APOSTLE_VERSION_H

namespace apostle {

// Human readable daemon version, matching the project() version in
// CMakeLists.txt.
inline constexpr const char *version = "0.1.0";

// Wire protocol revision this build speaks. See docs/PROTOCOL.md.
// Bumped only for a change that an existing peer cannot ignore.
inline constexpr int protocol_version = 1;

// Loopback port the Evangelist browser extension posts frames to.
inline constexpr int default_ingest_port = 8420;

// Network port Disciples connect to for the broadcast stream.
inline constexpr int default_broadcast_port = 8421;

// Largest frame the ingest endpoint accepts, in bytes. Reported to
// Evangelist as maxFrameBytes by /api/v1/health, so the extension can
// refuse a capture before spending loopback bandwidth on it. Eight
// megabytes is far more than a PNG of a browser window needs and far
// less than a body that could exhaust the daemon.
inline constexpr long long max_frame_bytes = 8 * 1024 * 1024;

} // namespace apostle

#endif // APOSTLE_VERSION_H
