//
// Command line options for the Disciple reader.
//
// A reader has no command line: inkerface calls program() with no
// arguments there, and every default is what a reader wants. The options
// are for running it on a desktop, in inkerface's emulation. Separated
// from the entry point so the parser can be tested on its own.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_OPTIONS_H
#define DISCIPLE_OPTIONS_H

#include "disciple/version.h"

#include <iosfwd>
#include <string>

namespace disciple {

//
// Everything the reader can be told from the command line.
//
struct options {
    // Apostle to join. Empty, the default, means find one by discovery,
    // which is the only way a reader can: it has no keyboard to be told
    // an address with.
    std::string host;
    int port = default_broadcast_port;

    // Go straight to Wi-Fi setup even with a network saved.
    bool wifi_setup = false;

    // Set by --help and --version, which print and exit.
    bool show_help = false;
    bool show_version = false;
};

//
// Print the usage text.
//
// Parameters:
//      out         - Stream to write to. Usage goes to stdout when it was
//                    asked for and to stderr when it follows a mistake.
//
void print_usage(std::ostream &out);

//
// Parse the command line.
//
// Parameters:
//      argc, argv  - Arguments as handed to program(). argc may be 0.
//      opts        - Receives the parsed options. Untouched fields keep
//                    their defaults.
//      error       - Receives a message describing the problem when
//                    parsing fails.
//
// Returns:
//      True when the command line was understood.
//
bool parse_arguments(int argc, char *argv[], options &opts,
                     std::string &error);

} // namespace disciple

#endif // DISCIPLE_OPTIONS_H
