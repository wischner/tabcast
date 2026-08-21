//
// Tests for the Disciple panel description.
//
// These numbers are small, constant, and easy to get wrong in a way
// that no compiler will catch: four grey LEVELS is two bits per pixel,
// while "four bit grey" would be sixteen levels, four times the data,
// and a depth the panel cannot display. That mistake was actually made
// in this file's first draft, so the relationship is asserted rather
// than assumed.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/panel.h"
#include "disciple/version.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

//
// Report one assertion.
//
void check(bool passed, const char *what) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", what);
    if (!passed) {
        ++failures;
    }
}

// The whole panel description is constexpr, so the important checks
// can happen at compile time. If any of these fail the build stops,
// which is stricter than a failing test and exactly what is wanted for
// a value that everything downstream sizes itself from.
static_assert(disciple::panel_width == 480,
              "panel width must match the target device");
static_assert(disciple::panel_height == 800,
              "panel height must match the target device");
static_assert(disciple::panel_gray_levels == 4,
              "the panel shows four shades");
static_assert(disciple::panel_gray_bits() == 2,
              "four levels is two bits per pixel, not four");
static_assert(disciple::panel_frame_bytes() == 96000,
              "480 x 800 at two bits is 96000 bytes");

// The derivation has to hold for every depth an e-ink panel might
// have, not only this one, because the next device will change the
// level count and nothing else.
static_assert((1 << disciple::panel_gray_bits()) >=
                  disciple::panel_gray_levels,
              "the bit count must be able to hold every level");
static_assert((1 << (disciple::panel_gray_bits() - 1)) <
                  disciple::panel_gray_levels,
              "the bit count must not be larger than necessary");

} // namespace

//
// Build a solid colour RGBA source image.
//
std::vector<std::uint8_t> solid(int width, int height, std::uint8_t r,
                                std::uint8_t g, std::uint8_t b) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = 255;
    }
    return pixels;
}

//
// Quantising must land on the panel's levels and nowhere between.
//
void test_quantise() {
    check(disciple::quantise(0) == 0, "black stays black");
    check(disciple::quantise(255) == 255, "white stays white");
    check(disciple::quantise(85) == 85, "an exact level is unchanged");
    check(disciple::quantise(80) == 85, "a near miss snaps to its level");
    check(disciple::quantise(200) == 170 || disciple::quantise(200) == 255,
          "a midpoint snaps to one of its neighbours");

    // Every possible input must produce one of exactly four values.
    bool all_on_levels = true;
    for (int v = 0; v <= 255; ++v) {
        const int q = disciple::quantise(v);
        if (q != 0 && q != 85 && q != 170 && q != 255) {
            all_on_levels = false;
        }
    }
    check(all_on_levels, "every input maps onto one of the 4 levels");

    // Out of range input must be clamped, not wrapped.
    check(disciple::quantise(-40) == 0, "negative clamps to black");
    check(disciple::quantise(400) == 255, "over-bright clamps to white");
}

//
// Grey conversion has to weight the channels, not average them.
//
void test_luminance() {
    check(disciple::luminance(255, 255, 255) == 255,
          "white converts to 255 exactly");
    check(disciple::luminance(0, 0, 0) == 0, "black converts to 0");

    const int red = disciple::luminance(255, 0, 0);
    const int green = disciple::luminance(0, 255, 0);
    const int blue = disciple::luminance(0, 0, 255);
    check(green > red && red > blue,
          "green reads lighter than red, red lighter than blue");
    check(blue < 40, "pure blue is dark, as Rec. 709 says");
}

//
// The image is fitted and centred, never stretched.
//
void test_fit() {
    // A square source on a portrait panel must gain white margins
    // above and below, not be stretched down the full height.
    const auto square = solid(100, 100, 0, 0, 0);
    const disciple::panel_frame frame =
        disciple::render_to_panel(square.data(), 100, 100);

    // 480 wide is the limiting dimension, so the drawn area is 480
    // tall and centred, leaving 160 pixels of margin above and below.
    check(frame.at(240, 0) == 255, "top margin is white");
    check(frame.at(240, 799) == 255, "bottom margin is white");
    check(frame.at(240, 400) == 0, "the centre carries the image");
    check(frame.at(240, 165) == 0, "the drawn area starts near y=160");
    check(frame.at(240, 155) == 255, "and the margin ends there");
}

//
// A source with the panel's own aspect ratio should fill it entirely.
//
void test_exact_aspect() {
    const auto page = solid(720, 1200, 0, 0, 0);
    const disciple::panel_frame frame =
        disciple::render_to_panel(page.data(), 720, 1200);

    check(frame.at(0, 0) == 0 && frame.at(479, 799) == 0,
          "a 3:5 source fills a 3:5 panel corner to corner");
}

//
// Packing must produce the exact bytes described in PROTOCOL.md.
//
void test_pack() {
    disciple::panel_frame frame;
    check(frame.at(0, 0) == 255, "a new frame is blank white paper");

    // A blank white frame packs to level 3 everywhere, which is every
    // bit set.
    const std::vector<std::uint8_t> blank = frame.pack();
    check(blank.size() ==
              static_cast<std::size_t>(disciple::panel_frame_bytes()),
          "packed size matches panel_frame_bytes()");
    check(blank[0] == 0xFF, "white packs to all bits set");

    // Set the first four pixels to levels 0, 1, 2, 3. Packed most
    // significant bits first, that is 00 01 10 11, or 0x1B.
    std::uint8_t *pixels = frame.pixels();
    pixels[0] = 0;
    pixels[1] = 85;
    pixels[2] = 170;
    pixels[3] = 255;

    const std::vector<std::uint8_t> packed = frame.pack();
    check(packed[0] == 0x1B,
          "the leftmost pixel occupies the most significant bits");

    // Rows are byte aligned: 480 pixels at 2 bits is exactly 120
    // bytes, so row 1 starts at offset 120 and is untouched white.
    check(packed[120] == 0xFF, "row 1 starts on a byte boundary");
}

int main() {
    check(disciple::panel_width == 480 && disciple::panel_height == 800,
          "panel is 480 x 800, portrait");
    check(disciple::panel_gray_levels == 4,
          "panel shows 4 grey levels");
    check(disciple::panel_gray_bits() == 2,
          "4 levels is 2 bits per pixel, not 4");
    check(disciple::panel_frame_bytes() == 96000,
          "a packed frame is 96000 bytes");

    // A frame at one byte per pixel is what the reader would have to
    // carry if Apostle did not pack. Worth stating, because it is the
    // number that justifies the packed format existing at all.
    const int unpacked = disciple::panel_width * disciple::panel_height;
    check(unpacked == 384000, "the same frame unpacked is 384000 bytes");
    check(unpacked / disciple::panel_frame_bytes() == 4,
          "packing saves a factor of four");

    test_quantise();
    test_luminance();
    test_fit();
    test_exact_aspect();
    test_pack();

    std::printf("\n%s\n", failures == 0 ? "all checks passed"
                                        : "there were failures");
    return failures == 0 ? 0 : 1;
}
