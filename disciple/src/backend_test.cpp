//
// The backend that opens a window on a development machine.
//
// Selected by BACKEND=test. Built on gtkmm, the C++ binding for GTK 3,
// which supplies the window, the event loop, and, through gdk-pixbuf,
// the PNG and JPEG codecs.
//
// This is the test client. It shows the frame exactly as the reader
// will: fitted to 480 by 800 and reduced to four grey levels, with no
// smoothing to flatter it. A page that is unreadable here will be
// unreadable on the device.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/backend.h"

#include <gdkmm/pixbuf.h>
#include <gdkmm/pixbufloader.h>
#include <gdkmm/wrap_init.h>
#include <glibmm/init.h>
#include <glibmm/main.h>
#include <gtkmm/application.h>
#include <gtkmm/image.h>
#include <gtkmm/window.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace disciple {
namespace {

// The bus name this client registers under. NON_UNIQUE because a
// second copy should open a second window rather than hand its
// arguments to the first one: comparing two renderings side by side is
// exactly what this tool is for.
constexpr const char *application_id = "uk.co.wischner.tabcast.disciple";

//
// Paint a panel frame into a new pixbuf, magnified by a whole number.
//
// Parameters:
//      frame       - The frame to expand.
//      zoom        - Whole number magnification, at least one.
//
// Returns:
//      An opaque RGB pixbuf, panel_frame::width * zoom by
//      panel_frame::height * zoom.
//
// Notes:
//      Magnification repeats pixels rather than interpolating them,
//      which is why this fills the pixbuf itself instead of handing
//      the work to Gdk::Pixbuf::scale_simple(). A smoothed preview
//      would hide exactly the hard quantisation edges this tool
//      exists to show.
//
//      Rows are addressed through the pixbuf's own rowstride, which
//      gdk-pixbuf pads to a multiple of four bytes and which is
//      therefore not width times channels.
//
Glib::RefPtr<Gdk::Pixbuf> to_pixbuf(const panel_frame &frame, int zoom) {
    const int width = panel_frame::width * zoom;
    const int height = panel_frame::height * zoom;

    Glib::RefPtr<Gdk::Pixbuf> image =
        Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, width, height);

    const int stride = image->get_rowstride();
    const int channels = image->get_n_channels();
    guint8 *pixels = image->get_pixels();

    for (int y = 0; y < height; ++y) {
        guint8 *row = pixels + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t grey = frame.at(x / zoom, y / zoom);
            guint8 *pixel = row + static_cast<std::size_t>(x) * channels;
            pixel[0] = grey;
            pixel[1] = grey;
            pixel[2] = grey;
        }
    }
    return image;
}

//
// Whether there is a display server to open a window on.
//
// Notes:
//      GTK exits the process when it cannot reach one, which is an
//      abrupt way to tell someone they are on an ssh session. Asking
//      first is what turns that into a sentence they can act on.
//
bool has_display() {
    if (std::getenv("DISPLAY") != nullptr ||
        std::getenv("WAYLAND_DISPLAY") != nullptr) {
        return true;
    }
    std::cerr << "disciple: no display to open a window on, use "
                 "--save with --no-view instead\n";
    return false;
}

//
// The window title, naming the panel it is standing in for.
//
std::string window_title() {
    return "Disciple: " + std::to_string(panel_frame::width) + "x" +
           std::to_string(panel_frame::height) + ", " +
           std::to_string(panel_gray_levels) + " greys";
}

//
// A window that can be redrawn, over a Gtk::Image.
//
class gtk_live_view final : public live_view {
public:
    //
    // Construct over the image the window is showing.
    //
    // Parameters:
    //      picture     - The image widget, outliving this view.
    //      zoom        - Magnification, fixed for the window's life.
    //
    gtk_live_view(Gtk::Image &picture, int zoom)
        : _picture(picture)
        , _zoom(zoom) {
    }

    void show(const panel_frame &frame) override {
        _picture.set(to_pixbuf(frame, _zoom));
    }

private:
    Gtk::Image &_picture;
    int _zoom;
};

class test_backend final : public backend {
public:
    //
    // Register gtkmm's C++ wrappers for the GObject types this
    // backend uses.
    //
    // Notes:
    //      Normally Gtk::Application does this, but constructing one
    //      calls gtk_init(), which prints "cannot open display" and
    //      exits the process when there is no display server. That
    //      would take --save --no-view and even --version down with
    //      it over ssh, so the application is left until view() and
    //      the two init calls it would have made are made here
    //      instead. Neither of them touches a display.
    //
    //      Without them Gdk::PixbufLoader decodes into a wrapper that
    //      cannot be built, which is a warning followed by a
    //      segmentation fault rather than an error.
    //
    test_backend() {
        Glib::init();
        Gdk::wrap_init();
    }

    const char *name() const override {
        return "test (gtkmm window on this machine)";
    }

    bool can_display() const override {
        return true;
    }

    bool decode(const std::uint8_t *data, std::size_t size,
                rgba_image &out, std::string &error) override {
        try {
            Glib::RefPtr<Gdk::PixbufLoader> loader =
                Gdk::PixbufLoader::create();
            loader->write(data, size);
            loader->close();

            Glib::RefPtr<Gdk::Pixbuf> image = loader->get_pixbuf();
            if (!image) {
                error = "no image in those bytes";
                return false;
            }

            // A PNG or JPEG without transparency decodes to three
            // channels. The panel model wants four, always.
            if (!image->get_has_alpha()) {
                image = image->add_alpha(false, 0, 0, 0);
            }

            out.width = image->get_width();
            out.height = image->get_height();

            const std::size_t row_bytes =
                static_cast<std::size_t>(out.width) * 4;
            out.pixels.resize(row_bytes * out.height);

            const int stride = image->get_rowstride();
            const guint8 *source = image->get_pixels();
            for (int y = 0; y < out.height; ++y) {
                std::copy_n(source + static_cast<std::size_t>(y) * stride,
                            row_bytes,
                            out.pixels.begin() +
                                static_cast<std::size_t>(y) * row_bytes);
            }
            return true;
        } catch (const Glib::Error &failure) {
            error = failure.what().c_str();
            return false;
        }
    }

    int watch(int descriptor,
              const std::function<bool(live_view &)> &on_readable,
              int zoom) override {
        const int magnification = std::max(1, zoom);
        if (!has_display()) {
            return EXIT_FAILURE;
        }

        const Glib::RefPtr<Gtk::Application> app =
            Gtk::Application::create(application_id,
                                     Gio::APPLICATION_NON_UNIQUE);

        Gtk::Window window;
        window.set_title(window_title());
        window.set_resizable(false);

        // Blank white paper until the first frame arrives, so the
        // window opens at the panel's size rather than growing into
        // it and shoving the desktop around.
        Gtk::Image picture(to_pixbuf(panel_frame(), magnification));
        window.add(picture);
        picture.show();

        gtk_live_view live(picture, magnification);

        // The toolkit's own loop does the waiting. This is the whole
        // reason watch() exists rather than a thread beside view().
        Glib::signal_io().connect(
            [&](Glib::IOCondition) {
                if (!on_readable(live)) {
                    window.hide();
                    return false;
                }
                return true;
            },
            descriptor,
            Glib::IO_IN | Glib::IO_HUP | Glib::IO_ERR);

        return app->run(window);
    }

    int view(const panel_frame &frame, int zoom) override {
        const int magnification = std::max(1, zoom);
        if (!has_display()) {
            return EXIT_FAILURE;
        }

        const Glib::RefPtr<Gtk::Application> app =
            Gtk::Application::create(application_id,
                                     Gio::APPLICATION_NON_UNIQUE);

        Gtk::Window window;
        window.set_title(window_title());
        // The frame is already at its final size. A resizable window
        // would only offer to stretch it, which is the one thing this
        // preview must never do.
        window.set_resizable(false);

        Gtk::Image picture(to_pixbuf(frame, magnification));
        window.add(picture);
        picture.show();

        return app->run(window);
    }

    bool save(const panel_frame &frame, const std::string &path,
              std::string &error) override {
        try {
            // Saved at panel size, never magnified. The file is a
            // record of what the device shows, not of the preview.
            to_pixbuf(frame, 1)->save(path, "png");
            return true;
        } catch (const Glib::Error &failure) {
            error = failure.what().c_str();
            return false;
        }
    }
};

} // namespace

std::unique_ptr<backend> make_backend() {
    return std::make_unique<test_backend>();
}

} // namespace disciple
