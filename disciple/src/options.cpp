//
// Command line parsing for the Disciple reader.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/options.h"

#include <charconv>
#include <ostream>
#include <string_view>

namespace disciple {
namespace {

//
// Parse a port number, 1 to 65535.
//
bool parse_port(std::string_view text, int &port) {
    int parsed = 0;
    const char *end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 1 ||
        parsed > 65535) {
        return false;
    }
    port = parsed;
    return true;
}

//
// Fetch the value that follows an option.
//
bool take_value(int argc, char *argv[], int &index, std::string_view &value) {
    if (index + 1 >= argc) {
        return false;
    }
    value = argv[++index];
    return true;
}

} // namespace

void print_usage(std::ostream &out) {
    out << "usage: disciple [options]\n"
        << "\n"
        << "Tabcast e-ink reader. Gets onto Wi-Fi, finds an Apostle daemon "
           "on the LAN,\n"
        << "joins it, and shows every page it broadcasts. Runs on inkerface: "
           "on a\n"
        << "reader with no arguments, on a desktop in inkerface's emulation "
           "(set\n"
        << "INKERFACE_PREVIEW=1 for its window).\n"
        << "\n"
        << "options:\n"
        << "      --host HOST     join this Apostle instead of looking for "
           "one\n"
        << "      --port PORT     broadcast port, tcp to join and udp to look "
           "on\n"
        << "                      (default " << default_broadcast_port << ")\n"
        << "      --wifi-setup    go straight to Wi-Fi setup, even with a "
           "network\n"
        << "                      saved\n"
        << "      --version       print the version and exit\n"
        << "  -h, --help          print this text and exit\n";
}

bool parse_arguments(int argc, char *argv[], options &opts,
                     std::string &error) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        std::string_view value;

        if (arg == "--help" || arg == "-h") {
            opts.show_help = true;
            return true;
        }
        if (arg == "--version") {
            opts.show_version = true;
            return true;
        }
        if (arg == "--wifi-setup") {
            opts.wifi_setup = true;
            continue;
        }
        if (arg == "--host") {
            if (!take_value(argc, argv, i, value)) {
                error = "--host needs an address";
                return false;
            }
            opts.host.assign(value);
            continue;
        }
        if (arg == "--port") {
            if (!take_value(argc, argv, i, value)) {
                error = "--port needs a port number";
                return false;
            }
            if (!parse_port(value, opts.port)) {
                error = std::string(value) + " is not a valid port";
                return false;
            }
            continue;
        }
        error = "unknown option " + std::string(arg);
        return false;
    }
    return true;
}

} // namespace disciple
