//
// A reader's whole life, from switching on to showing pages.
//
// Every stage keeps something that will move it on: a socket polled on
// each step, or the timer, or both. That is the invariant that keeps a
// reader from ever simply stopping, and every function below ends by
// setting one up.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/reader.h"

#include <algorithm>
#include <array>
#include <exception>
#include <ostream>
#include <utility>
#include <vector>

namespace disciple {
namespace {

using namespace std::chrono_literals;

// The longest the main loop waits on the buttons before looking at the
// sockets again.
constexpr auto tick = 100ms;

// How long to wait before each attempt after a failure, and how often to
// ask again while nobody answers. Quick at first, because the usual cause
// is a daemon a moment from starting, then slow enough that a reader left
// on all night costs nothing.
constexpr std::array retry_delays = {1s, 2s, 4s, 8s, 15s};

// Room in the receive buffer beyond one whole page message, for a ping or
// an ack's worth of whatever arrives behind it.
constexpr std::size_t page_slack = 1024;

// Apostle accepts and welcomes at once on a LAN. One that takes longer
// than this to welcome is not going to.
constexpr auto handshake_timeout = 5s;

// Apostle pings after keepalive seconds of silence and the reader
// answers, so on a healthy link something arrives at least that often.
// Twice that, plus a little for the daemon's one second tick, never
// fires on a live link and still notices a dead one before the song ends.
constexpr int silence_slack_seconds = 5;

} // namespace

reader::reader(inkerface::network &net, inkerface::storage &disks,
               panel &view, reader_options options, std::ostream &log)
    : _net(net)
    , _view(view)
    , _options(std::move(options))
    , _log(log)
    , _setup(net, disks, view, _timer, log, _options.setup_port) {
    // The page buffer, set aside now, before Wi-Fi takes its share of the
    // heap and breaks up what is left.
    _link.reserve(envelope_bytes + frame_header_bytes + view.page_bytes() +
                  page_slack);
}

reader::~reader() {
    _link.close();
}

const std::string &reader::name() const {
    return _setup.reader_name();
}

void reader::start() {
    _view.splash("press a button for Wi-Fi setup");
    _stage = stage::splash;
    _timer.set(std::chrono::milliseconds(std::max(0, _options.splash_ms)),
               [this]() { go_online(); });
}

void reader::step(const inkerface::input_snapshot &buttons) {
    if (_off) {
        return;
    }
    if (buttons.power) {
        switch_off();
        return;
    }

    // A reader that knows a network goes straight to it, so the splash is
    // the one moment to ask for setup instead. Any button but power.
    if (buttons.any() && _stage == stage::splash && !_options.wifi_setup) {
        _options.wifi_setup = true;
        note("button", "Wi-Fi setup after the splash");
    }

    // A reader has no one to report to but its log, and nobody to restart
    // it. Whatever goes wrong in one step, running out of memory included,
    // is reported and the reader starts looking for Apostle again, rather
    // than leaving the program and switching the radio off.
    try {
        _setup.poll();
        if (_finder.is_open()) {
            hear_answer();
        }
        if (_link.is_open()) {
            read_link();
        }
        _timer.fire_if_due();
    } catch (const std::exception &failure) {
        note("error", failure.what());
        retry(std::string("error: ") + failure.what());
    }
}

bool reader::switched_off() const {
    return _off;
}

//
// Say goodbye and put the off screen up. The panel keeps it while the
// reader sleeps, so it says what state the reader is in.
//
void reader::switch_off() {
    note("power", "switching off");
    _timer.cancel();
    _link.close();
    _finder.close();
    _view.status("Switched off", "press the power button to switch on");
    _off = true;
}

std::chrono::milliseconds reader::wait() const {
    return _timer.remaining(tick);
}

//
// Get onto Wi-Fi, unless the network is up already.
//
void reader::go_online() {
    if (_net.connected() && !_options.wifi_setup) {
        search();
        return;
    }
    _stage = stage::setup;
    _setup.start(_options.wifi_setup, [this]() {
        // Wi-Fi setup drew its own screens.
        _stage = stage::setup;
        search();
    });
}

//
// Look for an Apostle, or connect to the one we were given.
//
// Notes:
//      Called again by the timer while nobody answers, which re-sends
//      the discover. The socket stays open between asks, so a late
//      answer to an earlier one still counts.
//
void reader::search() {
    show(stage::connecting);

    if (!_options.host.empty()) {
        connect_to(_options.host, _options.port);
        return;
    }

    if (!_finder.is_open()) {
        std::string error;
        if (!_finder.open(_net, error)) {
            retry(error);
            return;
        }
    }
    if (!_looking) {
        note("looking", "for an apostle on udp port " +
                            std::to_string(_options.port));
        _looking = true;
    }
    if (!_finder.ask(_options.port)) {
        const std::string problem = "cannot send a discover yet";
        if (problem != _last_note) {
            note("waiting", problem);
            _last_note = problem;
        }
    }
    _timer.set(next_delay(), [this]() { search(); });
}

//
// Answers to a discover, if any have arrived.
//
void reader::hear_answer() {
    apostle_address found;
    std::string refusal;
    if (!_finder.read_answer(found, refusal)) {
        if (!refusal.empty() && refusal != _last_note) {
            note("ignored", refusal);
            _last_note = refusal;
        }
        return;
    }

    _finder.close();
    _looking = false;
    _last_note.clear();
    note("found", "apostle " +
                      (found.name.empty() ? std::string()
                                          : "on " + found.name + " ") +
                      "at " + found.host + ":" + std::to_string(found.port));
    connect_to(found.host, found.port);
}

//
// Connect, say hello, and wait for the welcome.
//
void reader::connect_to(const std::string &host, int port) {
    const inkerface::size panel_size = _view.extent();
    reader_identity who;
    who.name = name();
    who.width = panel_size.width;
    who.height = panel_size.height;

    std::string error;
    if (!_link.open(_net, host, port, who, error)) {
        retry(error);
        return;
    }
    _timer.set(handshake_timeout, [this]() {
        retry("apostle at " + _link.peer() + " did not answer the hello");
    });
}

//
// Bytes from Apostle: a welcome, frames, pings, or a goodbye.
//
void reader::read_link() {
    const bool was_joined = _link.joined();
    bool heard = false;
    int drawn = 0;
    std::string problem;

    // Frames are drawn as they complete, straight from the link's buffer,
    // including any that arrived just before a goodbye.
    const bool alive = _link.read_ready(
        [this, &drawn](const frame_header &header,
                       std::span<const std::uint8_t> image) {
            draw(header, image);
            ++drawn;
        },
        heard, problem);

    if (!was_joined && _link.joined()) {
        _attempt = 0;
        note("joined", "as " + _link.client_id() + ", keepalive " +
                           std::to_string(_link.keepalive_seconds()) + "s");
        // Apostle sends the current page straight after the welcome when
        // it has one, often in the same read. Saying "waiting" first
        // would cost a panel refresh for nothing.
        if (drawn == 0) {
            show(stage::waiting);
        }
    }

    if (!alive) {
        retry(problem.empty() ? "apostle closed the connection" : problem);
        return;
    }
    if (heard && _link.joined()) {
        watch_for_silence();
    }
}

//
// Give up on Apostle if nothing more arrives in time. Re-armed whenever
// anything is heard, so the deadline runs from the last word.
//
void reader::watch_for_silence() {
    const int seconds = 2 * _link.keepalive_seconds() + silence_slack_seconds;
    _timer.set(std::chrono::seconds(seconds),
               [this]() { retry("apostle went silent"); });
}

//
// Close everything and look again after a pause.
//
void reader::retry(const std::string &why) {
    _link.close();
    _finder.close();
    _looking = false;

    const auto delay = next_delay();
    note("lost", why + ", trying again in " +
                     std::to_string(
                         std::chrono::ceil<std::chrono::seconds>(delay).count()) +
                     "s");
    show(stage::connecting);
    _timer.set(delay, [this]() { search(); });
}

//
// Put a page on the panel, and acknowledge it.
//
// Notes:
//      Apostle sends this reader its panel's own pixels, one bit each,
//      so a page is drawn as it arrived: no decoder, no copy. A frame in
//      any other form is a daemon that ignored the hello.
//
void reader::draw(const frame_header &header,
                  std::span<const std::uint8_t> image) {
    const auto started = std::chrono::steady_clock::now();

    if (header.format != frame_format::gray_packed ||
        header.gray_levels != reader_gray_levels ||
        !_view.page(image, header.width, header.height)) {
        note("skipped",
             "frame " + std::to_string(header.sequence) + " is not packed " +
                 "for this panel at " + std::to_string(reader_gray_levels) +
                 " levels");
        return;
    }
    _stage = stage::page;

    const auto elapsed = std::chrono::steady_clock::now() - started;
    _link.acknowledge(
        header.sequence,
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

//
// Put a status screen up, unless a page is up.
//
void reader::show(stage screen) {
    if (_view.showing_page()) {
        return;
    }
    if (screen == stage::waiting) {
        _view.status("Connected", "waiting for the first page");
    } else {
        _view.status("Connecting...", "");
    }
    _stage = screen;
}

//
// The pause before the next attempt, growing with each failure.
//
std::chrono::milliseconds reader::next_delay() {
    const std::size_t index = std::min<std::size_t>(
        static_cast<std::size_t>(_attempt), retry_delays.size() - 1);
    ++_attempt;
    return retry_delays[index];
}

//
// Report one event, in the same columns as the start-up banner.
//
void reader::note(const char *label, const std::string &text) {
    std::string padded(label);
    padded.resize(std::max<std::size_t>(padded.size(), 8), ' ');
    _log << "  " << padded << " " << text << std::endl;
}

} // namespace disciple
