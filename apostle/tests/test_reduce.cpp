//
// Tests for reducing a page to a reader's panel: the panel limits, the
// decoder, the fit, the quantisation, and the packing, checked against
// the byte layout docs/PROTOCOL.md gives for image/gray-packed.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/reduce.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace apostle;

int failures = 0;

void check(bool passed, const std::string &what) {
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << "\n";
    if (!passed) {
        ++failures;
    }
}

// A 2 by 2 PNG: black and white on top, white and black below.
const std::vector<std::uint8_t> checker_png = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73, 0x00, 0x00, 0x00,
    0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0x60, 0xf8,
    0xff, 0xff, 0x3f, 0x18, 0x33, 0x30, 0x00, 0x00, 0x29, 0xe4, 0x05, 0xfb,
    0xd5, 0x93, 0xd9, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82};

//
// A page of one grey everywhere.
//
grey_image flat(int width, int height, std::uint8_t grey) {
    grey_image page;
    page.width = width;
    page.height = height;
    page.pixels.assign(static_cast<std::size_t>(width) * height, grey);
    return page;
}

void test_panel_spec() {
    check(panel_spec{480, 800, 4}.valid(), "the X4 at four levels is valid");
    check(panel_spec{480, 800, 2}.valid(), "a one bit panel is valid");
    check(!panel_spec{480, 800, 1}.valid(), "one level is not a panel");
    check(!panel_spec{480, 800, 5}.valid(),
          "three bits a pixel cannot be packed whole");
    check(!panel_spec{480, 800, 32}.valid(), "more than 16 levels is refused");
    check(!panel_spec{0, 800, 4}.valid() && !panel_spec{5000, 800, 4}.valid(),
          "absurd sizes are refused");

    check(panel_spec{480, 800, 4}.bits() == 2, "four levels is two bits");
    check(panel_spec{480, 800, 16}.bits() == 4, "sixteen levels is four bits");
    check(panel_spec{480, 800, 4}.packed_bytes() == 96000,
          "the X4 frame at four levels is 96000 bytes");
    check(panel_spec{480, 800, 2}.packed_bytes() == 48000,
          "the X4 frame at one bit is 48000 bytes");
    check(panel_spec{481, 2, 4}.packed_bytes() == 242,
          "rows are padded to whole bytes");
}

void test_decode() {
    grey_image page;
    std::string error;
    check(decode_to_grey(checker_png, page, error) && page.width == 2 &&
              page.height == 2,
          "a PNG decodes");
    check(page.pixels.size() == 4 && page.pixels[0] == 0 &&
              page.pixels[1] == 255 && page.pixels[2] == 255 &&
              page.pixels[3] == 0,
          "it decodes to luminance, row by row");

    std::vector<std::uint8_t> broken = checker_png;
    broken.resize(20);
    check(!decode_to_grey(broken, page, error) && !error.empty(),
          "a truncated image is refused with a reason");
    check(!decode_to_grey({}, page, error), "nothing is not an image");
}

void test_reduce() {
    // A page as tall as the panel's aspect, all black, fills it.
    const std::vector<std::uint8_t> black =
        reduce_to_panel(flat(240, 400, 0), panel_spec{480, 800, 4});
    bool all_black = black.size() == 96000;
    for (const std::uint8_t byte : black) {
        all_black = all_black && byte == 0;
    }
    check(all_black, "a black page of the panel's shape packs to zeros");

    // A wide page is fitted with white above and below.
    const std::vector<std::uint8_t> wide =
        reduce_to_panel(flat(480, 240, 0), panel_spec{480, 800, 4});
    check(wide[0] == 0xFF, "the top margin is white, all bits set");
    check(wide[120 * 400] == 0x00, "the middle row carries the page");
    check(wide[120 * 799] == 0xFF, "the bottom margin is white");

    // Greys land on the nearest of four levels: 85 is level 1.
    const std::vector<std::uint8_t> grey =
        reduce_to_panel(flat(4, 1, 85), panel_spec{4, 1, 4});
    check(grey.size() == 1 && grey[0] == 0x55,
          "a grey pixel packs as its level index, leftmost in the high bits");

    // One bit: a dark grey goes black and a light one white, no dither.
    const std::vector<std::uint8_t> dark =
        reduce_to_panel(flat(8, 1, 100), panel_spec{8, 1, 2});
    const std::vector<std::uint8_t> light =
        reduce_to_panel(flat(8, 1, 160), panel_spec{8, 1, 2});
    check(dark[0] == 0x00 && light[0] == 0xFF,
          "at one bit a page is thresholded, never dithered");

    // Downscaling averages: a 2 by 2 checker over one pixel is mid grey.
    grey_image checker;
    std::string error;
    decode_to_grey(checker_png, checker, error);
    const std::vector<std::uint8_t> one =
        reduce_to_panel(checker, panel_spec{1, 1, 4});
    check(one.size() == 1 && ((one[0] >> 6) == 1 || (one[0] >> 6) == 2),
          "downscaling averages rather than samples");
}

} // namespace

int main() {
    test_panel_spec();
    test_decode();
    test_reduce();

    std::cout << "\n"
              << (failures == 0 ? "all checks passed"
                                : "some checks failed")
              << "\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
