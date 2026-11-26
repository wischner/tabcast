//
// The pages a phone sees during Wi-Fi setup.
//
// Plain HTML with the style inline and nothing fetched from anywhere:
// the phone is on the reader's own network, which leads nowhere else.
// Sized for a phone held in one hand and laid out so the network list,
// the password, and the button fit on one screen.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_SETUP_PAGE_H
#define DISCIPLE_SETUP_PAGE_H

#include <inkerface/network.h>

#include <string>
#include <vector>

namespace disciple {

//
// What the setup form shows.
//
struct setup_form {
    // The reader's access point name, so the phone shows which
    // reader it is setting up.
    std::string reader;

    // Networks in range, strongest first.
    std::vector<inkerface::wifi_network> networks;

    // Network to have selected, usually the one last tried.
    std::string chosen;

    // Why the last attempt failed, shown above the form. Empty when
    // there is nothing to report.
    std::string problem;
};

//
// The setup form.
//
// Returns:
//      A whole HTML document. It posts ssid choice and password to
//      /join and offers /?rescan=1 to look for networks again.
//
std::string setup_page_html(const setup_form &form);

//
// The page shown once a network has been chosen.
//
// Parameters:
//      reader      - The reader's access point name.
//      ssid        - The network it is about to join.
//
// Notes:
//      The reader leaves its own network straight after sending this,
//      so the phone loses the connection. The page says so, and says
//      to watch the reader's screen, which is where the outcome
//      appears.
//
std::string joining_page_html(const std::string &reader,
                              const std::string &ssid);

} // namespace disciple

#endif // DISCIPLE_SETUP_PAGE_H
