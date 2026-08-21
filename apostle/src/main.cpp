//
// Apostle entry point.
//
// Parses the command line and reports what the daemon would listen on.
// The two listeners themselves, loopback ingest from Evangelist and
// the broadcast stream to the Disciples, are not implemented yet, so
// this refuses to run rather than pretending to serve.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/version.h"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

//
// Everything the daemon can be told from the command line.
//
struct options {
    int ingest_port = apostle::default_ingest_port;
    int broadcast_port = apostle::default_broadcast_port;
    bool show_help = false;
    bool show_version = false;
};

//
// Print the usage text.
//
// Parameters:
//      out         - Stream to write to. Usage goes to stdout when it
//                    was asked for and to stderr when it follows a
//                    mistake.
//
void print_usage(std::ostream &out) {
    out << "usage: apostle [options]\n"
        << "\n"
        << "Tabcast broadcast daemon. Accepts frames from the "
           "Evangelist browser\n"
        << "extension and distributes them to every registered "
           "Disciple.\n"
        << "\n"
        << "options:\n"
        << "  --ingest-port PORT     loopback port Evangelist posts "
           "frames to\n"
        << "                         (default "
        << apostle::default_ingest_port << ")\n"
        << "  --broadcast-port PORT  port Disciples connect to\n"
        << "                         (default "
        << apostle::default_broadcast_port << ")\n"
        << "  --version              print the version and exit\n"
        << "  --help                 print this text and exit\n";
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

        const bool is_ingest = arg == "--ingest-port";
        const bool is_broadcast = arg == "--broadcast-port";

        if (is_ingest || is_broadcast) {
            if (i + 1 >= argc) {
                std::cerr << "apostle: " << arg
                          << " needs a port number\n";
                return false;
            }
            int &target = is_ingest ? opts.ingest_port
                                    : opts.broadcast_port;
            if (!parse_port(argv[++i], target)) {
                std::cerr << "apostle: " << argv[i]
                          << " is not a valid port\n";
                return false;
            }
            continue;
        }

        std::cerr << "apostle: unknown option " << arg << "\n";
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
        std::cout << "apostle " << apostle::version
                  << " (protocol " << apostle::protocol_version << ")\n";
        return EXIT_SUCCESS;
    }

    std::cout << "apostle " << apostle::version << "\n"
              << "  ingest    http://127.0.0.1:" << opts.ingest_port
              << "/api/v1/frame\n"
              << "  broadcast tcp/*:" << opts.broadcast_port << "\n";

    // Deliberately loud. A daemon that starts up quietly and serves
    // nothing is far harder to diagnose than one that says so.
    std::cerr << "apostle: the listeners are not implemented yet\n";
    return EXIT_FAILURE;
}
