//
// Everything the reader puts on its panel: its own screens, and the
// pages Apostle sends.
//
// Drawn with inkerface's surface and its built-in fonts, so the screens
// look the same on every reader inkerface supports and are laid out from
// the panel's own size rather than the X4's. A page arrives already
// reduced by Apostle to this panel, one bit a pixel, and is drawn in one
// call with no copy.
//
// Every redraw of e-paper is a visible refresh, so a screen that is
// already up is not drawn again, and the refresh policy lives here.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_SCREENS_H
#define DISCIPLE_SCREENS_H

#include <inkerface/display.h>
#include <inkerface/geometry.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace disciple {

//
// The reader's panel.
//
class panel {
public:
    //
    // Draw on a display.
    //
    // Parameters:
    //      screen      - The display. Outlives the panel.
    //
    explicit panel(inkerface::display &screen);

    //
    // The panel's size in pixels, as the display reports it.
    //
    inkerface::size extent() const;

    //
    // Bytes one page occupies for this panel, one bit a pixel, every row
    // padded to a whole byte.
    //
    std::size_t page_bytes() const;

    //
    // The screen shown when the reader is switched on.
    //
    // Parameters:
    //      hint        - A line at the foot, such as how to reach Wi-Fi
    //                    setup. May be empty.
    //
    void splash(std::string_view hint);

    //
    // A one-message status screen.
    //
    // Parameters:
    //      title       - The message, large, in the middle.
    //      detail      - A smaller line under it. May be empty.
    //
    // Sample call:
    //      view.status("Connecting...", "");
    //
    void status(std::string_view title, std::string_view detail);

    //
    // The Wi-Fi setup screen: how to reach the reader from a phone.
    //
    // Parameters:
    //      network     - The reader's own access point name.
    //      password    - Its WPA2 password.
    //      address     - Where the setup page is, for a phone that does
    //                    not open it by itself.
    //      problem     - Why the last attempt to join failed. Empty on
    //                    first setup.
    //
    void setup(std::string_view network, std::string_view password,
               std::string_view address, std::string_view problem);

    //
    // Show a page.
    //
    // Parameters:
    //      packed      - The page, one bit a pixel for exactly this
    //                    panel: the leftmost pixel in the most
    //                    significant bit, rows padded to whole bytes, a
    //                    set bit for white. This is image/gray-packed at
    //                    two levels, as Apostle sends it.
    //      width,
    //      height      - The size the page was packed for.
    //
    // Returns:
    //      True when drawn. False when it was packed for another panel.
    //
    bool page(std::span<const std::uint8_t> packed, int width, int height);

    //
    // Whether a page is on the panel, rather than one of the reader's
    // own screens.
    //
    bool showing_page() const;

private:
    void present();

    inkerface::display &_screen;
    inkerface::size _extent;

    // What is on the panel now, so the same screen is not drawn twice.
    std::string _shown;

    // Frames presented so far, which decides when a full refresh is due.
    int _presented = 0;
};

} // namespace disciple

#endif // DISCIPLE_SCREENS_H
