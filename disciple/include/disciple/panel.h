//
// The panel model: what a captured page looks like once it has been
// reduced to what the reader can physically show.
//
// This is the heart of the client and it deliberately depends on
// nothing. No image library, no display, no sockets. It takes RGBA
// pixels in and produces the exact bytes the panel will display, which
// means it can be tested without a window, a device, or a decoder.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_PANEL_H
#define DISCIPLE_PANEL_H

#include "disciple/version.h"

#include <cstdint>
#include <vector>

namespace disciple {

//
// One frame, already reduced to the panel's size and grey depth.
//
// Stored one byte per pixel for the convenience of everything that
// reads it, but only panel_gray_levels distinct values ever appear.
// pack() produces the tight form that goes over the wire.
//
class panel_frame {
public:
    //
    // Create a blank white frame at the panel's size.
    //
    panel_frame();

    // Panel width in pixels.
    static constexpr int width = panel_width;

    // Panel height in pixels.
    static constexpr int height = panel_height;

    // Read-only access to the first pixel, row major.
    const std::uint8_t *pixels() const;

    // Mutable access to the first pixel, row major.
    std::uint8_t *pixels();

    //
    // Read one pixel.
    //
    // Parameters:
    //      x, y        - Position, which must be inside the panel.
    //
    // Returns:
    //      The grey value, one of the panel's levels.
    //
    std::uint8_t at(int x, int y) const;

    //
    // Pack the frame at the panel's real bit depth.
    //
    // Returns:
    //      panel_frame_bytes() bytes. Pixels run left to right with
    //      the most significant bits of a byte holding the leftmost
    //      pixel, and every row starts on a byte boundary. This is
    //      the `gray-packed` format in docs/PROTOCOL.md.
    //
    // Notes:
    //      Values are stored as level indices, 0 for black up to
    //      panel_gray_levels - 1 for white, not as 0-255 greys.
    //
    std::vector<std::uint8_t> pack() const;

private:
    std::vector<std::uint8_t> _pixels;
};

//
// Reduce an eight bit grey value to one of the panel's levels.
//
// Parameters:
//      grey        - Value from 0 to 255.
//
// Returns:
//      The nearest level, expressed back in the 0 to 255 range so it
//      can be displayed directly. For four levels that is 0, 85, 170,
//      or 255.
//
// Notes:
//      Deliberately not dithered. Dithering wins on photographs and
//      loses badly on text: it turns solid black letters into grey
//      speckle and softens every stroke. This pipeline carries lyric
//      and hymn sheets, so text wins.
//
std::uint8_t quantise(int grey);

//
// Convert a colour to grey.
//
// Parameters:
//      r, g, b     - Channel values from 0 to 255.
//
// Returns:
//      Rec. 709 luminance, from 0 to 255.
//
// Notes:
//      Rec. 709 weights rather than a flat average, because a flat
//      average renders blue text far too light and red text far too
//      dark on a grey panel.
//
std::uint8_t luminance(int r, int g, int b);

//
// Render an RGBA image onto the panel.
//
// Parameters:
//      rgba        - Source pixels, four bytes each, row major.
//      width       - Source width in pixels.
//      height      - Source height in pixels.
//
// Returns:
//      A frame at the panel's size and grey depth.
//
// Notes:
//      The image is fitted, not stretched. It is scaled by the same
//      factor on both axes and centred, with white where the aspect
//      ratios differ. Stretching would distort every letterform, and
//      a reader looking at squashed text would rightly blame the
//      device.
//
//      Downscaling averages every source pixel that falls under a
//      destination pixel rather than sampling one of them. On text
//      that is the difference between readable grey edges and a
//      broken, aliased mess.
//
// Sample call:
//      panel_frame frame = render_to_panel(pixels, 1200, 720);
//
panel_frame render_to_panel(const std::uint8_t *rgba, int width,
                            int height);

} // namespace disciple

#endif // DISCIPLE_PANEL_H
