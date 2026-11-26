//
// What a reader remembers, on inkerface's internal volume, and the names
// and passwords it makes up for its own access point.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/settings.h"

#include "disciple/wire.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>

#include <inkerface/file.h>

namespace disciple {
namespace {

// No i, l, o, 0, or 1: a password read off a small grey screen and
// typed on a phone should not need a second look.
constexpr const char *password_alphabet =
    "abcdefghjkmnpqrstuvwxyz23456789";

constexpr int password_length = 8;

// The settings file on the internal volume, and the most it can hold.
constexpr std::string_view settings_path = "disciple.json";
constexpr std::size_t settings_limit = 4096;

//
// Draw a number from the system's random source.
//
// Notes:
//      std::random_device may throw where the C library has no entropy
//      source it knows, as on some microcontroller toolchains. A name or
//      a setup password then still has to come from somewhere, so the
//      fallback mixes the clock with a counter. That is not secret, but
//      it only has to differ between readers and power-ons.
//
std::uint32_t random_word() {
    try {
        std::random_device source;
        return static_cast<std::uint32_t>(source());
    } catch (...) {
        static std::uint32_t counter = 0;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::uint64_t mixed = now + 0x9e3779b97f4a7c15ULL * ++counter;
        mixed ^= mixed >> 33;
        mixed *= 0xff51afd7ed558ccdULL;
        mixed ^= mixed >> 33;
        return static_cast<std::uint32_t>(mixed);
    }
}

} // namespace

bool reader_settings::has_network() const {
    return !ssid.empty();
}

std::string new_reader_name() {
    char name[16];
    std::snprintf(name, sizeof(name), "sheep_%08x",
                  static_cast<unsigned>(random_word()));
    return name;
}

std::string new_access_point_password() {
    const std::size_t count = std::strlen(password_alphabet);
    std::string password;
    for (int i = 0; i < password_length; ++i) {
        password += password_alphabet[random_word() % count];
    }
    return password;
}

std::string settings_to_json(const reader_settings &settings) {
    return std::string("{\"ssid\":") + json_quoted(settings.ssid) +
           ",\"password\":" + json_quoted(settings.password) +
           ",\"name\":" + json_quoted(settings.name) + "}\n";
}

bool settings_from_json(std::string_view text, reader_settings &out) {
    reader_settings read;
    json_string(text, "ssid", read.ssid);
    json_string(text, "password", read.password);
    json_string(text, "name", read.name);
    if (!read.has_network() && read.name.empty()) {
        return false;
    }
    out = read;
    return true;
}

bool settings_load(inkerface::storage &disks, reader_settings &out) {
    if (!disks.available(inkerface::volume::internal)) {
        return false;
    }
    inkerface::file file(disks, inkerface::volume::internal, settings_path,
                         inkerface::file_mode::read);
    if (!file) {
        return false;
    }
    std::string text(settings_limit, '\0');
    const std::size_t got =
        file.read(std::as_writable_bytes(std::span(text.data(), text.size())));
    text.resize(got);
    return settings_from_json(text, out);
}

bool settings_save(inkerface::storage &disks, const reader_settings &settings,
                   std::string &error) {
    if (!disks.available(inkerface::volume::internal)) {
        error = "no internal storage, settings last until power-off";
        return false;
    }
    inkerface::file file(disks, inkerface::volume::internal, settings_path,
                         inkerface::file_mode::write);
    const std::string text = settings_to_json(settings);
    if (!file || file.write(text) != text.size() || !file.close()) {
        error = "cannot write the settings to internal storage";
        return false;
    }
    return true;
}

} // namespace disciple
