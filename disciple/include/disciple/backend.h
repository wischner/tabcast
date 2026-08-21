//
// The display backend: everything that differs between running on a
// development machine and running on a reader.
//
// Three of them are planned and two exist. `test` opens a window on
// this machine through gtkmm and is the test client. `none` builds
// with no dependencies at all and can still run the protocol, it just
// has nowhere to draw. `freeink` will drive the real panel.
//
// Image decoding sits behind this interface too, which looks odd until
// you notice that every backend already has a decoder: gtkmm brings
// gdk-pixbuf, and the reader's SDK brings its own. Putting decoding
// here keeps a second copy of libpng out of the build.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_BACKEND_H
#define DISCIPLE_BACKEND_H

#include "disciple/panel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace disciple {

//
// A decoded image, four bytes per pixel, row major.
//
struct rgba_image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

//
// A window that stays open and can be redrawn.
//
// Handed to the callback in backend::watch() so a newly arrived frame
// can be put on screen without the caller knowing what a window is.
//
class live_view {
public:
    virtual ~live_view() = default;

    //
    // Replace what the window is showing.
    //
    // Parameters:
    //      frame       - The frame, already reduced to the panel.
    //
    virtual void show(const panel_frame &frame) = 0;
};

//
// What this build can do with a frame.
//
class backend {
public:
    virtual ~backend() = default;

    //
    // Name this backend for the banner, so it is never a mystery
    // which one a given binary was built with.
    //
    virtual const char *name() const = 0;

    //
    // Whether this build can put anything on a screen.
    //
    virtual bool can_display() const = 0;

    //
    // Decode an encoded image held in memory.
    //
    // Parameters:
    //      data, size  - PNG or JPEG bytes, as they arrive on the
    //                    wire.
    //      out         - Receives the decoded image.
    //      error       - Receives a message when decoding fails.
    //
    // Returns:
    //      True on success.
    //
    virtual bool decode(const std::uint8_t *data, std::size_t size,
                        rgba_image &out, std::string &error) = 0;

    //
    // Show a frame and wait until the viewer closes it.
    //
    // Parameters:
    //      frame       - The frame, already reduced to the panel.
    //      zoom        - Whole number magnification, so a 480 by 800
    //                    panel is legible on a high density monitor.
    //
    // Returns:
    //      A process exit code. A backend that cannot display returns
    //      immediately rather than failing.
    //
    // Notes:
    //      Showing and waiting are one call because GTK owns the
    //      event loop once it starts. Streaming will need this split
    //      into a present() plus a pumped loop, which is a change to
    //      make when there is a stream to pump.
    //
    virtual int view(const panel_frame &frame, int zoom) = 0;

    //
    // Open a window and keep it updated from a descriptor.
    //
    // Parameters:
    //      descriptor  - Socket to watch. The callback runs whenever
    //                    it becomes readable.
    //      on_readable - Called with the window each time there is
    //                    something to read. Returns false to close
    //                    the window and return.
    //      zoom        - Whole number magnification, as for view().
    //
    // Returns:
    //      A process exit code.
    //
    // Notes:
    //      This is what view() could not be. GTK owns the event loop
    //      once it starts, so a client that must read a socket while
    //      a window is open cannot block on either one. Handing the
    //      descriptor to the backend lets the toolkit's own loop wait
    //      on both, with no second thread and no polling.
    //
    //      A backend that cannot display returns immediately rather
    //      than failing, the same way view() does.
    //
    virtual int watch(int descriptor,
                      const std::function<bool(live_view &)> &on_readable,
                      int zoom) = 0;

    //
    // Write a frame to a PNG file.
    //
    // Parameters:
    //      frame       - The frame, already reduced to the panel.
    //      path        - Destination file.
    //      error       - Receives a message when saving fails.
    //
    // Returns:
    //      True on success.
    //
    // Notes:
    //      Useful with no display at all: it is how you check what the
    //      reader would show from a script or over ssh.
    //
    virtual bool save(const panel_frame &frame, const std::string &path,
                      std::string &error) = 0;
};

//
// Create the backend this binary was built with.
//
// Returns:
//      An owned backend. Never null.
//
// Notes:
//      Which one is decided at compile time by the Makefile's BACKEND
//      variable, not at run time. There is exactly one implementation
//      of this function in any given build.
//
std::unique_ptr<backend> make_backend();

} // namespace disciple

#endif // DISCIPLE_BACKEND_H
