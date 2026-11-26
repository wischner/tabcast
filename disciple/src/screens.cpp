//
// Everything the reader puts on its panel.
//
// Layouts are proportions of the panel, so a reader with another panel
// gets the same composition rather than a cropped one. Text is black:
// inkerface would dither grey, and dithered text is speckled text.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/screens.h"

#include "disciple/version.h"

#include <inkerface/color.h>
#include <inkerface/font.h>
#include <inkerface/surface.h>

#include <algorithm>
#include <span>

namespace disciple {
namespace {

using inkerface::color;
using inkerface::coord;
using inkerface::font;
using inkerface::rect;
using inkerface::text_align;
using inkerface::text_style;

// A fast refresh leaves faint ghosts of earlier frames on e-paper. Every
// so many frames, a full, flashing refresh clears them.
constexpr int full_refresh_every = 8;

constexpr std::string_view wordmark = "tabcast";

coord to_coord(int value) {
    return static_cast<coord>(std::clamp(value, -32768, 32767));
}

//
// Draws lines down the panel, each centred, keeping track of where the
// next one goes.
//
class column {
public:
    column(inkerface::surface &out, int top)
        : _out(out)
        , _top(top) {
    }

    //
    // Draw one line and move down past it.
    //
    // Parameters:
    //      text        - The line. Wraps onto up to lines lines.
    //      face        - Font.
    //      gap         - Space to leave under it, in pixels.
    //      lines       - Most lines it may wrap onto.
    //
    void line(std::string_view text, font face, int gap, int lines = 1) {
        const int height = _out.line_height(face) * lines;
        const int margin = _out.extent().width / 16;
        text_style style;
        style.face = face;
        style.align = text_align::center;
        style.max_lines = static_cast<std::uint8_t>(lines);
        _out.draw_text(rect{to_coord(margin), to_coord(_top),
                            to_coord(_out.extent().width - 2 * margin),
                            to_coord(height)},
                       text, style);
        _top += height + gap;
    }

    // Move down without drawing.
    void skip(int pixels) {
        _top += pixels;
    }

    int top() const {
        return _top;
    }

private:
    inkerface::surface &_out;
    int _top;
};

//
// The wordmark small at the foot, so no screen is a bare sentence.
//
void foot(inkerface::surface &out) {
    const int height = out.line_height(font::small());
    const inkerface::size whole = out.extent();
    column(out, whole.height - whole.height / 20 - height)
        .line(wordmark, font::small(), 0);
}

} // namespace

panel::panel(inkerface::display &screen)
    : _screen(screen)
    , _extent(screen.frame().extent()) {
}

inkerface::size panel::extent() const {
    return _extent;
}

std::size_t panel::page_bytes() const {
    const auto row = static_cast<std::size_t>((_extent.width + 7) / 8);
    return row * static_cast<std::size_t>(_extent.height);
}

void panel::splash(std::string_view hint) {
    const std::string key = "splash|" + std::string(hint);
    if (_shown == key) {
        return;
    }
    inkerface::surface out = _screen.frame();
    out.clear(color::white);

    // The wordmark a little above the middle, as a title sits on a page,
    // with a rule under it as heavy as a stroke.
    const int title_height = out.line_height(font::title());
    column lines(out, _extent.height * 2 / 5 - title_height);
    lines.line(wordmark, font::title(), 8);
    const int rule = _extent.width / 3;
    out.fill_rect(rect{to_coord((_extent.width - rule) / 2),
                       to_coord(lines.top()), to_coord(rule), 4},
                  color::black);
    lines.skip(24);
    lines.line(std::string("disciple ") + version, font::small(), 0);

    if (!hint.empty()) {
        const int height = out.line_height(font::small());
        column(out, _extent.height - _extent.height / 20 - height)
            .line(hint, font::small(), 0);
    }
    _shown = key;
    present();
}

void panel::status(std::string_view title, std::string_view detail) {
    const std::string key =
        "status|" + std::string(title) + "|" + std::string(detail);
    if (_shown == key) {
        return;
    }
    inkerface::surface out = _screen.frame();
    out.clear(color::white);

    column lines(out, _extent.height / 2 - out.line_height(font::title()));
    lines.line(title, font::title(), 12);
    if (!detail.empty()) {
        lines.line(detail, font::regular(), 0, 2);
    }
    foot(out);
    _shown = key;
    present();
}

void panel::setup(std::string_view network, std::string_view password,
                  std::string_view address, std::string_view problem) {
    const std::string key = "setup|" + std::string(network) + "|" +
                            std::string(password) + "|" +
                            std::string(address) + "|" +
                            std::string(problem);
    if (_shown == key) {
        return;
    }
    inkerface::surface out = _screen.frame();
    out.clear(color::white);

    // The instructions in the regular face, what has to be typed in bold,
    // so the eye goes straight to what the phone needs.
    column lines(out, _extent.height / 12);
    lines.line("Wi-Fi setup", font::title(), 40);
    lines.line("1. On your phone, join", font::regular(), 8);
    lines.line(network, font::title(), 24);
    lines.line("password", font::small(), 4);
    lines.line(password, font::title(), 40);
    lines.line("2. If no page opens, go to", font::regular(), 8);
    lines.line(address, font::bold(), 40);
    if (!problem.empty()) {
        lines.line(problem, font::regular(), 0, 3);
    }
    foot(out);
    _shown = key;
    present();
}

bool panel::page(std::span<const std::uint8_t> packed, int width,
                 int height) {
    if (width != _extent.width || height != _extent.height ||
        packed.size() != page_bytes()) {
        return false;
    }
    inkerface::surface out = _screen.frame();

    // Set bits are white, so the page is drawn as white ink on black.
    // The packed rows are exactly the MSB-first bitmap inkerface draws,
    // so this is one call and no copy.
    out.fill_rect(rect{0, 0, _extent.width, _extent.height}, color::black);
    out.draw_bitmap({0, 0}, _extent, packed, color::white);

    _shown = "page";
    present();
    return true;
}

bool panel::showing_page() const {
    return _shown == "page";
}

//
// Push the frame to the panel, fast usually and full now and then.
//
void panel::present() {
    const bool full = _presented % full_refresh_every == 0;
    ++_presented;
    static_cast<void>(_screen.present(full ? inkerface::refresh_mode::full
                                           : inkerface::refresh_mode::fast));
}

} // namespace disciple
