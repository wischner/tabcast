//
// Panel model implementation.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/panel.h"

#include <algorithm>

namespace disciple {
namespace {

// Distance between two adjacent grey levels, in the 0 to 255 range.
// Four levels gives 85, so the levels are 0, 85, 170, and 255.
constexpr int level_step = 255 / (panel_gray_levels - 1);

// Bytes one packed row occupies, rounded up to a whole byte.
constexpr int packed_row_bytes =
    (panel_width * panel_gray_bits() + 7) / 8;

// Rec. 709 luminance weights, scaled by 1024 so the whole conversion
// is integer arithmetic. 218 + 732 + 74 is 1024 exactly, so a white
// pixel converts to exactly 255 rather than 254.
constexpr int weight_red = 218;
constexpr int weight_green = 732;
constexpr int weight_blue = 74;

//
// Average the luminance of a rectangle of source pixels.
//
// Parameters:
//      rgba        - Source pixels.
//      width       - Source width, needed for the row stride.
//      x0, y0      - Top left of the region, inclusive.
//      x1, y1      - Bottom right of the region, exclusive.
//
// Returns:
//      Mean luminance from 0 to 255.
//
// Notes:
//      This is what makes small text survive a large downscale. Taking
//      one sample per destination pixel would drop whole stems and
//      crossbars, and the result reads as a broken font rather than a
//      small one.
//
std::uint8_t average_luminance(const std::uint8_t *rgba, int width,
                               int x0, int y0, int x1, int y1) {
    long total = 0;
    long count = 0;

    for (int y = y0; y < y1; ++y) {
        const std::uint8_t *row = rgba + (static_cast<long>(y) * width +
                                          x0) * 4;
        for (int x = x0; x < x1; ++x, row += 4) {
            total += luminance(row[0], row[1], row[2]);
            ++count;
        }
    }
    return count > 0 ? static_cast<std::uint8_t>(total / count) : 255;
}

} // namespace

panel_frame::panel_frame()
    // White, which is the panel's rest state. A frame that fails to
    // render should look like blank paper, not a black rectangle.
    : _pixels(static_cast<std::size_t>(panel_width) * panel_height,
              255) {}

const std::uint8_t *panel_frame::pixels() const {
    return _pixels.data();
}

std::uint8_t *panel_frame::pixels() {
    return _pixels.data();
}

std::uint8_t panel_frame::at(int x, int y) const {
    return _pixels[static_cast<std::size_t>(y) * panel_width + x];
}

std::vector<std::uint8_t> panel_frame::pack() const {
    std::vector<std::uint8_t> packed(
        static_cast<std::size_t>(packed_row_bytes) * panel_height, 0);

    constexpr int bits = panel_gray_bits();
    constexpr int per_byte = 8 / bits;

    for (int y = 0; y < panel_height; ++y) {
        std::uint8_t *row = packed.data() +
                            static_cast<std::size_t>(y) * packed_row_bytes;

        for (int x = 0; x < panel_width; ++x) {
            // Levels go out as indices, not as 0 to 255 greys.
            const int level = at(x, y) / level_step;

            // Leftmost pixel occupies the most significant bits.
            const int slot = x % per_byte;
            const int shift = 8 - bits * (slot + 1);
            row[x / per_byte] |= static_cast<std::uint8_t>(level << shift);
        }
    }
    return packed;
}

std::uint8_t quantise(int grey) {
    const int clamped = std::clamp(grey, 0, 255);
    // Round to nearest level, then express it back as a grey so the
    // result can be displayed without a second lookup.
    const int level = (clamped + level_step / 2) / level_step;
    return static_cast<std::uint8_t>(
        std::min(level, panel_gray_levels - 1) * level_step);
}

std::uint8_t luminance(int r, int g, int b) {
    const int value =
        (r * weight_red + g * weight_green + b * weight_blue) / 1024;
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

panel_frame render_to_panel(const std::uint8_t *rgba, int width,
                            int height) {
    panel_frame frame;
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return frame;
    }

    // Fit rather than fill: one scale factor for both axes, so nothing
    // is distorted, and the smaller dimension gets white margins.
    const double scale = std::min(
        static_cast<double>(panel_width) / width,
        static_cast<double>(panel_height) / height);

    const int drawn_w =
        std::max(1, std::min(panel_width,
                             static_cast<int>(width * scale + 0.5)));
    const int drawn_h =
        std::max(1, std::min(panel_height,
                             static_cast<int>(height * scale + 0.5)));

    const int left = (panel_width - drawn_w) / 2;
    const int top = (panel_height - drawn_h) / 2;

    std::uint8_t *out = frame.pixels();

    for (int y = 0; y < drawn_h; ++y) {
        // Source rows this destination row covers. The span is at
        // least one row even when scaling up.
        const int sy0 = static_cast<int>(
            static_cast<long long>(y) * height / drawn_h);
        const int sy1 = std::max(
            sy0 + 1,
            static_cast<int>(static_cast<long long>(y + 1) * height /
                             drawn_h));

        for (int x = 0; x < drawn_w; ++x) {
            const int sx0 = static_cast<int>(
                static_cast<long long>(x) * width / drawn_w);
            const int sx1 = std::max(
                sx0 + 1,
                static_cast<int>(static_cast<long long>(x + 1) * width /
                                 drawn_w));

            const std::uint8_t grey = average_luminance(
                rgba, width, sx0, sy0, std::min(sx1, width),
                std::min(sy1, height));

            out[static_cast<std::size_t>(top + y) * panel_width +
                (left + x)] = quantise(grey);
        }
    }
    return frame;
}

} // namespace disciple
