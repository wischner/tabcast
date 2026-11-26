//
// Reducing a captured page to exactly what one reader's panel shows.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/reduce.h"

#include "stb/stb_image.h"

#include <algorithm>
#include <climits>

namespace apostle {
namespace {

constexpr int largest_side = 4096;
// Sixteen is as deep as e-ink panels go, and it keeps the level count
// within the one byte the frame header gives it.
constexpr int most_levels = 16;

// Rec. 709 luminance weights, scaled by 1024 so the conversion is
// integer arithmetic. They sum to 1024, so white stays exactly 255.
constexpr int weight_red = 218;
constexpr int weight_green = 732;
constexpr int weight_blue = 74;

//
// Mean luminance of a rectangle of the page, half-open.
//
std::uint8_t average(const grey_image &page, int x0, int y0, int x1,
                     int y1) {
    long total = 0;
    long count = 0;
    for (int y = y0; y < y1; ++y) {
        const std::uint8_t *row =
            page.pixels.data() + static_cast<std::size_t>(y) * page.width;
        for (int x = x0; x < x1; ++x) {
            total += row[x];
            ++count;
        }
    }
    return count > 0 ? static_cast<std::uint8_t>(total / count) : 255;
}

} // namespace

bool panel_spec::valid() const {
    if (width <= 0 || height <= 0 || width > largest_side ||
        height > largest_side || gray_levels < 2 ||
        gray_levels > most_levels) {
        return false;
    }
    // Packing puts whole pixels in a byte, so the depth must divide 8.
    const int depth = bits();
    return depth == 1 || depth == 2 || depth == 4;
}

int panel_spec::bits() const {
    int count = 0;
    while ((1 << count) < gray_levels) {
        ++count;
    }
    return count;
}

std::size_t panel_spec::packed_bytes() const {
    const std::size_t row =
        (static_cast<std::size_t>(width) * bits() + 7) / 8;
    return row * static_cast<std::size_t>(height);
}

bool decode_to_grey(const std::vector<std::uint8_t> &bytes,
                    grey_image &out, std::string &error) {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        error = "no image to decode";
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    // Always four channels out, whatever the file holds, so there is
    // one conversion and not four.
    stbi_uc *rgba = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()), &width, &height,
        &channels, 4);
    if (rgba == nullptr) {
        error = stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                 : "undecodable image";
        return false;
    }

    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<std::size_t>(width) * height);
    const stbi_uc *pixel = rgba;
    for (std::uint8_t &grey : out.pixels) {
        grey = static_cast<std::uint8_t>(
            (pixel[0] * weight_red + pixel[1] * weight_green +
             pixel[2] * weight_blue) /
            1024);
        pixel += 4;
    }
    stbi_image_free(rgba);
    return true;
}

std::vector<std::uint8_t> reduce_to_panel(const grey_image &page,
                                          const panel_spec &panel) {
    const int bits = panel.bits();
    const int white = panel.gray_levels - 1;
    const std::size_t row_bytes =
        (static_cast<std::size_t>(panel.width) * bits + 7) / 8;

    // One level index per panel pixel first, white paper by default.
    std::vector<std::uint8_t> levels(
        static_cast<std::size_t>(panel.width) * panel.height,
        static_cast<std::uint8_t>(white));

    if (page.width > 0 && page.height > 0) {
        const double scale =
            std::min(static_cast<double>(panel.width) / page.width,
                     static_cast<double>(panel.height) / page.height);
        const int drawn_w = std::max(
            1, std::min(panel.width,
                        static_cast<int>(page.width * scale + 0.5)));
        const int drawn_h = std::max(
            1, std::min(panel.height,
                        static_cast<int>(page.height * scale + 0.5)));
        const int left = (panel.width - drawn_w) / 2;
        const int top = (panel.height - drawn_h) / 2;

        for (int y = 0; y < drawn_h; ++y) {
            const int sy0 = static_cast<int>(
                static_cast<long long>(y) * page.height / drawn_h);
            const int sy1 = std::min(
                page.height,
                std::max(sy0 + 1,
                         static_cast<int>(static_cast<long long>(y + 1) *
                                          page.height / drawn_h)));
            for (int x = 0; x < drawn_w; ++x) {
                const int sx0 = static_cast<int>(
                    static_cast<long long>(x) * page.width / drawn_w);
                const int sx1 = std::min(
                    page.width,
                    std::max(sx0 + 1,
                             static_cast<int>(
                                 static_cast<long long>(x + 1) *
                                 page.width / drawn_w)));

                // Nearest level, rounding to the closer of the two.
                const int grey = average(page, sx0, sy0, sx1, sy1);
                const int level = (grey * white + 127) / 255;
                levels[static_cast<std::size_t>(top + y) * panel.width +
                       left + x] = static_cast<std::uint8_t>(level);
            }
        }
    }

    std::vector<std::uint8_t> packed(row_bytes * panel.height, 0);
    const int per_byte = 8 / bits;
    for (int y = 0; y < panel.height; ++y) {
        std::uint8_t *row = packed.data() + row_bytes * y;
        const std::uint8_t *source =
            levels.data() + static_cast<std::size_t>(y) * panel.width;
        for (int x = 0; x < panel.width; ++x) {
            // The leftmost pixel takes the most significant bits.
            const int shift = 8 - bits * (x % per_byte + 1);
            row[x / per_byte] |=
                static_cast<std::uint8_t>(source[x] << shift);
        }
    }
    return packed;
}

} // namespace apostle
