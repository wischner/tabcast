//
// Disciple version and device information.
//
// A single place for the client's version and for the panel geometry
// of the target reader, both of which go into the `hello` message the
// client sends when it joins the flock.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_VERSION_H
#define DISCIPLE_VERSION_H

namespace disciple {

// Human readable client version, matching the project() version in
// CMakeLists.txt.
inline constexpr const char *version = "0.1.0";

// Wire protocol revision this build speaks. See docs/PROTOCOL.md.
inline constexpr int protocol_version = 1;

// Port the Apostle daemon serves the broadcast stream on.
inline constexpr int default_broadcast_port = 8421;

// Device the client identifies itself as. The panel geometry below
// belongs to this device and must move with it.
inline constexpr const char *device_name = "XTEINK-X4";

// Panel geometry, reported to Apostle so it can scale frames on the
// host rather than on the reader's much slower processor.
//
// PLACEHOLDER. These numbers have not been read off an XTEINK X4 and
// must be confirmed against the device or the FreeInk SDK before the
// first real render. Everything downstream sizes itself from here, so
// getting them wrong is not a cosmetic mistake.
inline constexpr int panel_width = 1404;
inline constexpr int panel_height = 1872;

// Grey levels the panel can actually show, as bits per pixel.
// PLACEHOLDER, see the note above.
inline constexpr int panel_gray_bits = 4;

} // namespace disciple

#endif // DISCIPLE_VERSION_H
