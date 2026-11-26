//
// Tests for what the reader puts on its panel, on inkerface's desktop
// backend: the same FreeInk renderer as a reader, into an in-memory
// panel whose pixels can be read back.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/screens.h"

#include <inkerface/display.h>
#include <inkerface/surface.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace disciple;

int failures = 0;

void check(bool passed, const std::string &what) {
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << "\n";
    if (!passed) {
        ++failures;
    }
}

bool black(inkerface::display &screen, int x, int y) {
    const auto pixel = screen.frame().pixel_at(
        {static_cast<inkerface::coord>(x), static_cast<inkerface::coord>(y)});
    return pixel.has_value() && *pixel == inkerface::color::black;
}

//
// How many black pixels the panel shows.
//
long inked(inkerface::display &screen) {
    const inkerface::size extent = screen.frame().extent();
    long count = 0;
    for (int y = 0; y < extent.height; ++y) {
        for (int x = 0; x < extent.width; ++x) {
            count += black(screen, x, y) ? 1 : 0;
        }
    }
    return count;
}

void test_geometry(inkerface::display &screen, panel &view) {
    const inkerface::size extent = view.extent();
    check(extent.width == screen.frame().extent().width &&
              extent.height == screen.frame().extent().height,
          "the panel is the size the display reports");
    check(view.page_bytes() ==
              static_cast<std::size_t>((extent.width + 7) / 8 * extent.height),
          "a page is one bit a pixel, rows padded to whole bytes");
}

void test_screens(inkerface::display &screen, panel &view) {
    view.splash("press a button for Wi-Fi setup");
    const long splash = inked(screen);
    check(splash > 0, "the splash draws something");

    view.status("Connecting...", "");
    const long connecting = inked(screen);
    check(connecting > 0 && connecting != splash, "a status screen draws");

    view.status("Connected", "waiting for the first page");
    check(inked(screen) > 0, "a status screen with detail draws");

    view.setup("sheep_0badf00d", "k7mq4xvp", "192.168.4.1", "");
    const long setup = inked(screen);
    view.setup("sheep_0badf00d", "k7mq4xvp", "192.168.4.1",
               "Could not join Parish Office: wrong password, or out of range");
    check(setup > 0 && inked(screen) > setup,
          "the setup screen draws, and a problem adds to it");
    check(!view.showing_page(), "none of these is a page");
}

void test_page(inkerface::display &screen, panel &view) {
    const inkerface::size extent = view.extent();

    // All white but the first eight pixels of the first row.
    std::vector<std::uint8_t> packed(view.page_bytes(), 0xFF);
    packed[0] = 0x00;
    check(view.page(packed, extent.width, extent.height), "a page is drawn");
    check(view.showing_page(), "and it is a page on the panel");
    check(black(screen, 0, 0) && black(screen, 7, 0),
          "clear bits are black, leftmost pixel in the high bit");
    check(!black(screen, 8, 0) && !black(screen, extent.width - 1,
                                         extent.height - 1),
          "set bits are white");
    check(inked(screen) == 8, "and nothing else is drawn");

    // The last pixel, at the end of a padded row.
    std::fill(packed.begin(), packed.end(), 0xFF);
    packed.back() = 0xFE;
    view.page(packed, extent.width, extent.height);
    check(inked(screen) == (extent.width % 8 == 0 ? 1 : 0),
          "rows are byte aligned to the last pixel");

    check(!view.page(packed, extent.width + 8, extent.height),
          "a page packed for another panel is refused");
    std::vector<std::uint8_t> short_page(view.page_bytes() - 1, 0xFF);
    check(!view.page(short_page, extent.width, extent.height),
          "a page of the wrong length is refused");
}

} // namespace

int main() {
    inkerface::display screen;
    panel view(screen);

    test_geometry(screen, view);
    test_screens(screen, view);
    test_page(screen, view);

    std::cout << "\n"
              << (failures == 0 ? "all checks passed" : "some checks failed")
              << "\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
