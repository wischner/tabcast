//
// Getting a reader onto Wi-Fi, with a phone as its keyboard.
//
// What happens when a reader is switched on, before it looks for
// Apostle:
//
//   - It has a network saved: it joins it, showing "Connecting...".
//   - It has none, or joining fails: it becomes an access point named
//     after itself (sheep_ and eight hex digits), protected by a password
//     it shows on its panel. A phone that joins is taken straight to a
//     setup page by the captive portal, picks a network, types its
//     password, and the reader leaves its access point to join it. If
//     that works the choice is saved. If not, the reader comes back as
//     the same access point and the page says what went wrong.
//
// A saved network that is only briefly away, a router still booting
// when the reader is switched on, should not strand a reader in setup for
// good. So while in setup with a network saved, the reader tries that
// network again every couple of minutes, as long as no phone has been
// using the setup page.
//
// The radio is inkerface's network: a real soft access point on a
// reader, an emulated one on a desktop, where the setup page is served
// at the machine's own address so a real phone can still use it.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_WIFI_SETUP_H
#define DISCIPLE_WIFI_SETUP_H

#include "disciple/portal.h"
#include "disciple/screens.h"
#include "disciple/settings.h"
#include "disciple/timer.h"

#include <inkerface/network.h>
#include <inkerface/storage.h>

#include <chrono>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace disciple {

//
// One reader getting online.
//
class wifi_setup {
public:
    //
    // Load the reader's settings, choosing and saving its name on first
    // power-on. Nothing else happens yet.
    //
    // Parameters:
    //      net         - The radio. Outlives this.
    //      disks       - Where settings are kept. Outlives this.
    //      view        - The panel. Outlives this.
    //      clock       - The reader's timer, used until on_online.
    //      log         - Where progress is reported.
    //      port        - Port for the setup page; 0 for the usual one.
    //
    wifi_setup(inkerface::network &net, inkerface::storage &disks,
               panel &view, timer &clock, std::ostream &log, int port = 0);

    //
    // Leave access point mode, if in it.
    //
    ~wifi_setup();

    wifi_setup(const wifi_setup &) = delete;
    wifi_setup &operator=(const wifi_setup &) = delete;

    //
    // The reader's own name, sheep_ and eight hex digits.
    //
    const std::string &reader_name() const;

    //
    // Get online.
    //
    // Parameters:
    //      force_setup - Go straight to setup even with a network
    //                    saved. The way to move a reader to a new
    //                    network.
    //      on_online   - Called once, when a network is joined. By then
    //                    nothing of this class is waiting on the timer.
    //
    void start(bool force_setup, std::function<void()> on_online);

    //
    // Serve the phone: the setup page and DNS. Never waits.
    //
    void poll();

    //
    // Whether the reader is in setup, being its own access point.
    //
    bool in_setup() const;

private:
    using clock_type = std::chrono::steady_clock;

    void join(const std::string &ssid, const std::string &password);
    void enter_setup(const std::string &problem);
    void leave_setup();
    void retry_saved_when_idle();
    void rescan();
    http_reply answer(const http_request &request);
    http_reply take_form(const http_request &request);
    std::string page_address() const;
    int page_port() const;
    void note(const char *label, const std::string &text);

    inkerface::network &_net;
    inkerface::storage &_disks;
    panel &_view;
    timer &_clock;
    std::ostream &_log;
    int _port;
    std::function<void()> _on_online;

    // What is saved. A network chosen on the phone is saved only once
    // it has been joined, so a mistyped password never replaces one
    // that worked.
    reader_settings _saved;
    std::string _trying_ssid;

    // The access point, while in setup. The password is made up once
    // per power-on, so a phone that already has it keeps working when
    // the reader comes back after a failed join.
    bool _in_setup = false;
    bool _emulated = false;
    std::string _ap_password;
    std::string _address;
    std::vector<inkerface::wifi_network> _networks;
    std::string _problem;
    clock_type::time_point _last_visit;

    http_portal _portal;
    captive_dns _dns;
};

} // namespace disciple

#endif // DISCIPLE_WIFI_SETUP_H
