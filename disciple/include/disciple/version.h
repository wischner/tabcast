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
// The X4 is a portrait reader and these are its physical pixels, held
// the way the device is held. Everything downstream sizes itself from
// them, so a page is fitted to a tall panel rather than a wide one,
// which is what a reader actually shows.
inline constexpr int panel_width = 480;
inline constexpr int panel_height = 800;

// Distinct shades the panel can show, black and white included.
//
// Counted as LEVELS, not as bits. The two are easy to confuse and the
// confusion is expensive: four levels is two bits per pixel, while
// "four bit grey" would mean sixteen levels, four times the data and
// a picture the panel cannot actually display. Everything downstream
// quantises to this number, so it is stated in the unit that cannot
// be misread.
inline constexpr int panel_gray_levels = 4;

//
// Bits needed to store one pixel at the panel's grey depth.
//
// Returns:
//      Bits per pixel: 1 for two levels, 2 for four, 4 for sixteen.
//
// Notes:
//      Derived rather than stored, so it can never drift out of step
//      with panel_gray_levels.
//
constexpr int panel_gray_bits() {
    int bits = 0;
    while ((1 << bits) < panel_gray_levels) {
        ++bits;
    }
    return bits;
}

//
// Bytes one full frame occupies on the panel, packed at its grey
// depth.
//
// Returns:
//      Size in bytes of one tightly packed frame.
//
// Notes:
//      480 x 800 at two bits is 96000 bytes, against 384000 if the
//      same frame were carried one byte per pixel. That difference is
//      the argument for packing before it goes over the air to a
//      battery powered reader.
//
constexpr int panel_frame_bytes() {
    return panel_width * panel_height * panel_gray_bits() / 8;
}

} // namespace disciple

#endif // DISCIPLE_VERSION_H
