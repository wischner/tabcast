//
// Disciple version, protocol, and what the reader tells Apostle about
// its panel.
//
// The panel's size is not here. Disciple runs on inkerface, which runs
// on every reader it supports, so the size is whatever the display in
// hand reports (display::frame().extent()) and the hello says so. Only
// the grey depth is fixed, because inkerface draws one bit deep on every
// reader.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_VERSION_H
#define DISCIPLE_VERSION_H

namespace disciple {

// The version a release stamps in with -DTABCAST_VERSION, and the
// version of these sources otherwise.
#ifndef TABCAST_VERSION
#define TABCAST_VERSION "0.1.0"
#endif

// Human readable client version.
inline constexpr const char *version = TABCAST_VERSION;

// Wire protocol revision this build speaks. See docs/PROTOCOL.md.
inline constexpr int protocol_version = 1;

// Port the Apostle daemon serves the broadcast stream on, over TCP, and
// answers discovery on, over UDP.
inline constexpr int default_broadcast_port = 8421;

// Distinct shades the reader asks Apostle for, black and white included.
//
// Counted as LEVELS, not bits. inkerface's framebuffer is one bit deep on
// every reader and only dithers greys, so asking for more would buy grey
// speckle around every letter. Two levels gets crisp, thresholded pages
// at one bit a pixel: 48000 bytes for a 480 by 800 panel.
inline constexpr int reader_gray_levels = 2;

} // namespace disciple

#endif // DISCIPLE_VERSION_H
