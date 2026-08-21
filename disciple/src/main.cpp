//
// Disciple entry point.
//
// Parses the command line and reports which Apostle it would join and
// what it would tell the daemon about this panel. The socket, the
// protocol, and the FreeInk render path are not implemented yet, so
// this refuses to run rather than pretending to display anything.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/version.h"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

//
// Everything the client can be told from the command line.
//
struct options {
    std::string host = "127.0.0.1";
    int port = disciple::default_broadcast_port;
    bool show_help = false;
    bool show_version = false;
};

//
// Name of the render back end this binary was built with.
//
// Returns:
//      A short string for the banner, so it is never a mystery
//      whether a build can actually drive a panel.
//
constexpr const char *backend_name() {
#ifdef DISCIPLE_HEADLESS
    return "headless (no FreeInk SDK)";
#else
    return "FreeInk";
#endif
}

//
// Print the usage text.
//
// Parameters:
//      out         - Stream to write to. Usage goes to stdout when it
//                    was asked for and to stderr when it follows a
//                    mistake.
//
void print_usage(std::ostream &out) {
    out << "usage: disciple [options]\n"
        << "\n"
        << "Tabcast e-ink client. Joins an Apostle daemon and renders "
           "the frames\n"
        << "it broadcasts.\n"
        << "\n"
        << "options:\n"
        << "  --host HOST   Apostle to join (default 127.0.0.1)\n"
        << "  --port PORT   broadcast port (default "
        << disciple::default_broadcast_port << ")\n"
        << "  --version     print the version and exit\n"
        << "  --help        print this text and exit\n";
}

//
// Parse a decimal port number.
//
// Parameters:
//      text        - Argument text.
//      port        - Receives the parsed value when parsing succeeds.
//
// Returns:
//      True when the whole argument parsed as a port in range.
//
bool parse_port(std::string_view text, int &port) {
    int value = 0;
    const char *end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);

    if (result.ec != std::errc {} || result.ptr != end) {
        return false;
    }
    if (value < 1 || value > 65535) {
        return false;
    }
    port = value;
    return true;
}

//
// Parse the command line.
//
// Parameters:
//      argc, argv  - Arguments as handed to main().
//      opts        - Receives the parsed options.
//
// Returns:
//      True when the command line was understood. On false the caller
//      should exit with a failure status, the error has already been
//      reported.
//
bool parse_arguments(int argc, char *argv[], options &opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opts.show_help = true;
            return true;
        }
        if (arg == "--version") {
            opts.show_version = true;
            return true;
        }
        if (arg == "--host") {
            if (i + 1 >= argc) {
                std::cerr << "disciple: --host needs an address\n";
                return false;
            }
            opts.host = argv[++i];
            continue;
        }
        if (arg == "--port") {
            if (i + 1 >= argc) {
                std::cerr << "disciple: --port needs a port number\n";
                return false;
            }
            if (!parse_port(argv[++i], opts.port)) {
                std::cerr << "disciple: " << argv[i]
                          << " is not a valid port\n";
                return false;
            }
            continue;
        }

        std::cerr << "disciple: unknown option " << arg << "\n";
        print_usage(std::cerr);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    options opts;

    if (!parse_arguments(argc, argv, opts)) {
        return EXIT_FAILURE;
    }
    if (opts.show_help) {
        print_usage(std::cout);
        return EXIT_SUCCESS;
    }
    if (opts.show_version) {
        std::cout << "disciple " << disciple::version
                  << " (protocol " << disciple::protocol_version
                  << ", " << backend_name() << ")\n";
        return EXIT_SUCCESS;
    }

    std::cout << "disciple " << disciple::version << "\n"
              << "  apostle  tcp://" << opts.host << ":" << opts.port
              << "\n"
              << "  device   " << disciple::device_name << " "
              << disciple::panel_width << "x" << disciple::panel_height
              << " at " << disciple::panel_gray_bits << " bit grey\n"
              << "  backend  " << backend_name() << "\n";

    // Deliberately loud. A client that connects to nothing and shows
    // nothing is worse than one that says so and stops.
    std::cerr << "disciple: the broadcast client is not implemented "
                 "yet\n";
    return EXIT_FAILURE;
}
