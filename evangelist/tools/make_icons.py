#!/usr/bin/env python3
#
# Evangelist icon generator.
#
# Build tool, run from the project Makefile. Draws the toolbar icon at
# every size the manifest asks for and writes it as a PNG, using only
# the Python standard library, because the whole extension is built
# without a package manager.
#
# The mark is the broadcast glyph: a point of origin with three waves
# going out from it, which is the same idea the three parts of Tabcast
# are named after.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

import math
import os
import struct
import sys
import zlib

# Sizes the manifest references.
SIZES = (16, 32, 48, 128)

# Each pixel is rendered from this many samples per axis, then boxed
# down. Cheap antialiasing that costs nothing at these sizes.
SUPERSAMPLE = 4

# Ink for the rounded tile, and paper for the mark on top of it.
INK = (0x16, 0x18, 0x1D, 0xFF)
PAPER = (0xFA, 0xFA, 0xF9, 0xFF)

# Geometry in unit space, with the origin at the top left corner.
TILE_RADIUS = 0.22

# The mark is optically sized. At 16 pixels a 0.06 wide stroke lands on
# a single pixel and three waves smear into a grey smudge, so the small
# icon gets two fatter waves instead. This is the same reason a real
# typeface ships a caption cut.
FINE = {
    "origin": (0.29, 0.72),
    "dot": 0.075,
    "waves": (0.20, 0.33, 0.46),
    "thickness": 0.070,
}
COARSE = {
    "origin": (0.28, 0.73),
    "dot": 0.115,
    "waves": (0.30, 0.52),
    "thickness": 0.125,
}

# Icons this size or smaller use the coarse cut.
COARSE_MAX_SIZE = 16


def rounded_tile(x, y):
    """Test whether a unit-space point is inside the rounded tile.

    Uses the standard rounded box distance: push the point into the
    first quadrant, subtract the inner half extent, and measure how
    far outside the corner radius it lands.
    """
    qx = abs(x - 0.5) - (0.5 - TILE_RADIUS)
    qy = abs(y - 0.5) - (0.5 - TILE_RADIUS)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    inside = min(max(qx, qy), 0.0)
    return outside + inside - TILE_RADIUS <= 0.0


def broadcast_mark(x, y, cut):
    """Test whether a unit-space point is on the broadcast glyph.

    The glyph is a filled dot at the origin plus concentric arcs, each
    spanning the quadrant that opens up and to the right.

    Parameters:
        x, y    - Unit-space sample position.
        cut     - Geometry dictionary, FINE or COARSE.
    """
    dx = x - cut["origin"][0]
    dy = y - cut["origin"][1]
    distance = math.hypot(dx, dy)

    if distance <= cut["dot"]:
        return True

    # Only the up and right quadrant carries the waves.
    if dx < 0.0 or dy > 0.0:
        return False

    half = cut["thickness"] / 2.0
    return any(abs(distance - r) <= half for r in cut["waves"])


def sample(x, y, cut):
    """Return the RGBA colour of one unit-space sample."""
    if not rounded_tile(x, y):
        return (0, 0, 0, 0)
    return PAPER if broadcast_mark(x, y, cut) else INK


def render(size):
    """Render one icon.

    Parameters:
        size    - Edge length in pixels.

    Returns:
        A bytearray of size * size * 4 RGBA bytes.
    """
    hi = size * SUPERSAMPLE
    samples = SUPERSAMPLE * SUPERSAMPLE
    pixels = bytearray(size * size * 4)
    cut = COARSE if size <= COARSE_MAX_SIZE else FINE

    for py in range(size):
        for px in range(size):
            totals = [0, 0, 0, 0]
            for sy in range(SUPERSAMPLE):
                y = (py * SUPERSAMPLE + sy + 0.5) / hi
                for sx in range(SUPERSAMPLE):
                    x = (px * SUPERSAMPLE + sx + 0.5) / hi
                    r, g, b, a = sample(x, y, cut)
                    # Premultiply so a transparent sample cannot drag
                    # a colour fringe into the antialiased edge.
                    totals[0] += r * a
                    totals[1] += g * a
                    totals[2] += b * a
                    totals[3] += a

            alpha = totals[3] // samples
            offset = (py * size + px) * 4
            if alpha == 0:
                continue
            pixels[offset + 0] = min(255, totals[0] // totals[3])
            pixels[offset + 1] = min(255, totals[1] // totals[3])
            pixels[offset + 2] = min(255, totals[2] // totals[3])
            pixels[offset + 3] = alpha

    return pixels


def write_png(path, size, pixels):
    """Write RGBA pixels out as a PNG file.

    Parameters:
        path    - Destination file.
        size    - Edge length in pixels.
        pixels  - RGBA bytes, size * size * 4 of them.
    """
    stride = size * 4
    # Filter type 0, "none", in front of every scanline. These images
    # are tiny and flat, so a cleverer filter would not pay for the
    # code it costs.
    raw = b"".join(
        b"\x00" + bytes(pixels[row * stride:(row + 1) * stride])
        for row in range(size)
    )

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", header) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))

    with open(path, "wb") as handle:
        handle.write(png)


def main(argv):
    """Write every icon size into the directory given as argv[1]."""
    out_dir = argv[1] if len(argv) > 1 else "src/icons"
    os.makedirs(out_dir, exist_ok=True)

    for size in SIZES:
        path = os.path.join(out_dir, "icon-%d.png" % size)
        write_png(path, size, render(size))
        print("wrote %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
