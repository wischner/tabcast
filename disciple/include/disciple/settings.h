//
// What a reader remembers between power cycles, and the names and
// passwords it makes up for its own Wi-Fi access point.
//
// Kept as a small JSON file on inkerface's internal volume, which is
// flash on a reader and an image file on a desktop. Internal storage
// survives power loss and is always there, unlike a card.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_SETTINGS_H
#define DISCIPLE_SETTINGS_H

#include <inkerface/storage.h>

#include <string>
#include <string_view>

namespace disciple {

//
// What the reader remembers.
//
struct reader_settings {
    // The network to join, and its password. Empty until set up.
    std::string ssid;
    std::string password;

    // This reader's own name: sheep_ and eight hex digits. It names the
    // reader's access point during setup and the reader in Apostle's
    // log. Chosen once, at random, and kept, so a phone is told the
    // same name every time.
    std::string name;

    //
    // Whether a network has been set up.
    //
    bool has_network() const;
};

//
// Settings as a flat JSON object.
//
std::string settings_to_json(const reader_settings &settings);

//
// Settings from the JSON settings_to_json() wrote.
//
// Returns:
//      True when the text held settings.
//
bool settings_from_json(std::string_view text, reader_settings &out);

//
// Read the settings from internal storage.
//
// Returns:
//      True when there were any. False on first power-on, and when the
//      volume is unavailable, which is not an error: a desktop run with
//      no storage simply starts fresh.
//
bool settings_load(inkerface::storage &disks, reader_settings &out);

//
// Write the settings to internal storage.
//
// Returns:
//      True when saved. error says why not.
//
// Notes:
//      The password is kept as it was typed. The reader must give it to
//      the network again, so it cannot keep only a hash. Anyone who can
//      read the reader's storage can read the password, as with any
//      device that joins Wi-Fi.
//
bool settings_save(inkerface::storage &disks, const reader_settings &settings,
                   std::string &error);

//
// A fresh reader name: sheep_ and eight random hex digits.
//
// Sample call:
//      settings.name = new_reader_name();   // sheep_3fa91c07
//
std::string new_reader_name();

//
// A fresh access point password.
//
// Returns:
//      Eight characters from lowercase letters and digits, with the
//      ones that are easy to confuse on a small screen left out: no i,
//      l, o, 0, or 1.
//
std::string new_access_point_password();

} // namespace disciple

#endif // DISCIPLE_SETTINGS_H
