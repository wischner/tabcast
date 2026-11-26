//
// Reducing a captured page to exactly what one reader's panel shows.
//
// The `image/gray-packed` format of docs/PROTOCOL.md. A reader that
// accepts it is sent its panel's pixels and nothing more: the page
// fitted to the panel, averaged down, quantised to the panel's grey
// levels, and packed at the panel's bit depth. The reader then needs no
// image decoder and no memory for a full colour page, which is the only
// way a microcontroller reader can show a browser capture at all.
//
// The arithmetic is the same as Disciple's own render path (fit, area
// average, Rec. 709 luminance, nearest level, no dithering), so a page
// reduced here looks exactly like one a desktop Disciple reduced for
// itself.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_REDUCE_H
#define APOSTLE_REDUCE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace apostle {

//
// What a reader's panel can show, from its hello.
//
struct panel_spec {
    int width = 0;
    int height = 0;
    // Distinct shades, black and white included. Not bits.
    int gray_levels = 0;

    //
    // Whether a reader could really have this panel.
    //
    // Notes:
    //      Bounded so a hello cannot ask the daemon to allocate
    //      without limit: at most 4096 by 4096 pixels, and 2 to 16
    //      levels whose bit depth is 1, 2, or 4, so that a byte always
    //      holds whole pixels.
    //
    bool valid() const;

    //
    // Bits per pixel: ceil(log2(gray_levels)).
    //
    int bits() const;

    //
    // Bytes one packed frame occupies, rows padded to whole bytes.
    //
    std::size_t packed_bytes() const;

    bool operator==(const panel_spec &other) const = default;
};

//
// A page decoded to luminance, one byte per pixel.
//
// Notes:
//      Luminance rather than colour: every panel is grey, so the
//      colour is reduced once per page rather than once per reader.
//
struct grey_image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

//
// Decode a PNG or JPEG held in memory.
//
// Parameters:
//      bytes       - The encoded image, as Evangelist posted it.
//      out         - Receives the image as luminance.
//      error       - Receives why it could not be decoded.
//
// Returns:
//      True on success.
//
bool decode_to_grey(const std::vector<std::uint8_t> &bytes,
                    grey_image &out, std::string &error);

//
// Fit, reduce, and pack a page for one panel.
//
// Parameters:
//      page        - The decoded page.
//      panel       - The panel, which must be valid().
//
// Returns:
//      panel.packed_bytes() bytes. Pixels run left to right with the
//      most significant bits of a byte holding the leftmost pixel,
//      every row starts on a byte boundary, and each pixel is a level
//      index: 0 for black up to gray_levels - 1 for white.
//
// Notes:
//      Fitted, not stretched: one scale for both axes, centred, with
//      white where the aspect ratios differ. Downscaling averages every
//      source pixel under a panel pixel, which is what keeps small
//      text legible. Not dithered: dithering turns solid letters into
//      speckle, and these are lyric sheets.
//
std::vector<std::uint8_t> reduce_to_panel(const grey_image &page,
                                          const panel_spec &panel);

} // namespace apostle

#endif // APOSTLE_REDUCE_H
