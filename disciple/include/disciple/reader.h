//
// A reader's whole life, from switching on to showing pages.
//
// What the reader does needs no one to tell it anything:
//
//   1. Show the tabcast splash for a few seconds. A button pressed now
//      asks for Wi-Fi setup after it.
//
// At any point, holding the power button switches the reader off: it
// says goodbye to Apostle, shows "Switched off", and asks its main loop
// to call inkerface::power_off(). The next press of the power button
// boots it afresh, straight back to its network and Apostle.
//   2. Get onto Wi-Fi: join the saved network, or become an access point
//      and let a phone choose one. See wifi_setup.h.
//   3. Show "Connecting..." and look for an Apostle: broadcast a
//      discover, or connect straight to --host when one was given.
//   4. Join it. Until the first page arrives, say so.
//   5. Show every page Apostle sends.
//
// When the link is lost, or Apostle goes silent for longer than its
// keepalive allows, the reader goes back to step 3 by itself, waiting a
// little longer after each failure. A page already on the panel stays
// there while it does. A singer halfway through a verse is better served
// by the words they had than by a status screen.
//
// The reader is an ordinary inkerface program's state: the main loop
// waits on the buttons for at most wait(), then hands what it got to
// step(), which checks every socket without waiting and fires the timer
// when it is due. Nothing here blocks except joining a network and
// connecting to Apostle, both bounded, both behind "Connecting...".
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_READER_H
#define DISCIPLE_READER_H

#include "disciple/discovery.h"
#include "disciple/link.h"
#include "disciple/screens.h"
#include "disciple/timer.h"
#include "disciple/version.h"
#include "disciple/wifi_setup.h"

#include <inkerface/input.h>
#include <inkerface/network.h>
#include <inkerface/storage.h>

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>

namespace disciple {

//
// How long the splash stays up, in milliseconds.
//
inline constexpr int default_splash_ms = 3000;

//
// What a reader is told before it starts.
//
struct reader_options {
    // Apostle to join. Empty means find one by discovery, which is what
    // a reader always does.
    std::string host;

    // Broadcast port: the TCP port for host, and the UDP port a discover
    // is sent to.
    int port = default_broadcast_port;

    // How long the splash is shown.
    int splash_ms = default_splash_ms;

    // Go straight to Wi-Fi setup even with a network saved.
    bool wifi_setup = false;

    // Port for the Wi-Fi setup page. 0 means 80 on a reader and 8080
    // where inkerface emulates the access point on a desktop.
    int setup_port = 0;
};

//
// One reader, online or trying to be.
//
class reader {
public:
    //
    // Construct a reader that has not started. Loads its settings.
    //
    // Parameters:
    //      net         - The network. Outlives the reader.
    //      disks       - Where settings are kept. Outlives the reader.
    //      view        - The panel. Outlives the reader.
    //      options     - Where to look for Apostle.
    //      log         - Where progress is reported, one line an event.
    //
    reader(inkerface::network &net, inkerface::storage &disks, panel &view,
           reader_options options, std::ostream &log);

    //
    // Say bye to Apostle, if joined.
    //
    ~reader();

    reader(const reader &) = delete;
    reader &operator=(const reader &) = delete;

    //
    // Show the splash and set everything else in motion.
    //
    void start();

    //
    // Do whatever is due: react to buttons, read every socket, fire the
    // timer.
    //
    // Parameters:
    //      buttons     - What the input reported since the last step.
    //
    // Notes:
    //      Returns promptly. Call it in a loop, waiting on the input for
    //      at most wait() between calls.
    //
    void step(const inkerface::input_snapshot &buttons);

    //
    // How long the main loop may wait on the buttons before the next
    // step: until the timer is due, and never more than a tenth of a
    // second, so a page that arrives is shown without visible delay.
    //
    std::chrono::milliseconds wait() const;

    //
    // The reader's own name, sheep_ and eight hex digits.
    //
    const std::string &name() const;

    //
    // Whether the power button asked the reader to switch off. Once true,
    // the reader has said goodbye and drawn its off screen, and the main
    // loop should call inkerface::power_off() and stop stepping.
    //
    bool switched_off() const;

private:
    // What the reader's own screens last said, so a page is never
    // covered by a status screen and none is drawn twice.
    enum class stage { splash, setup, connecting, waiting, page };

    void switch_off();
    void go_online();
    void search();
    void hear_answer();
    void connect_to(const std::string &host, int port);
    void read_link();
    void retry(const std::string &why);
    void watch_for_silence();
    void draw(const frame_header &header, std::span<const std::uint8_t> image);
    void show(stage screen);
    std::chrono::milliseconds next_delay();
    void note(const char *label, const std::string &text);

    inkerface::network &_net;
    panel &_view;
    reader_options _options;
    std::ostream &_log;

    timer _timer;
    wifi_setup _setup;
    discovery _finder;
    apostle_link _link;

    stage _stage = stage::splash;
    bool _off = false;

    // Failed attempts since the last successful join, which is what the
    // next delay grows from.
    int _attempt = 0;

    // Set while looking, so a search that asks several times reports
    // that it is looking only once.
    bool _looking = false;

    // The last refusal reported, so a daemon that keeps refusing is not
    // reported every time it is asked.
    std::string _last_note;
};

} // namespace disciple

#endif // DISCIPLE_READER_H
