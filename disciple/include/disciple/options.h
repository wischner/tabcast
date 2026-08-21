//
// Command line options for the Disciple client.
//
// Separated from the entry point so the parser can be tested without a
// window or a socket. See tests/test_options.cpp.
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

// Largest magnification the viewer will accept. Eight times a 480 by
// 800 panel is already 3840 by 6400, which is past any desk.
inline constexpr int max_zoom = 8;

//
// Everything the client can be told from the command line.
//
struct options {
    // Apostle to join, when streaming.
    std::string host = "127.0.0.1";
    int port = default_broadcast_port;

    // Show this image file instead of connecting. The whole render
    // path runs, which is what makes the client useful before the
    // daemon can serve anything.
    std::string replay;

    // Write the rendered panel frame here, as a PNG.
    std::string save;

    // Whole number magnification for the viewer window.
    int zoom = 1;

    // Render without opening a window. Pairs with --save to turn the
    // client into a batch renderer, which is what a script or a CI job
    // wants.
    bool no_view = false;

    // Set by --help and --version, which print and exit.
    bool show_help = false;
    bool show_version = false;

    //
    // Whether the client should replay a file rather than connect.
    //
    bool is_replay() const;
};

//
// Print the usage text.
//
// Parameters:
//      out         - Stream to write to. Usage goes to stdout when it
//                    was asked for and to stderr when it follows a
//                    mistake.
//
void print_usage(std::ostream &out);

//
// Parse the command line.
//
// Parameters:
//      argc, argv  - Arguments as handed to the entry point.
//      opts        - Receives the parsed options. Untouched fields
//                    keep their defaults.
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
