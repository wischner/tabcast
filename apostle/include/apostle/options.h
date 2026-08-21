//
// Command line options for the Apostle daemon.
//
// Separated from main.cpp so the parser can be tested without starting
// a daemon. See tests/test_options.cpp.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_OPTIONS_H
#define APOSTLE_OPTIONS_H

#include "apostle/log.h"
#include "apostle/version.h"

#include <iosfwd>
#include <string>

namespace apostle {

//
// How the daemon runs.
//
enum class run_mode {
    // Stay attached to the terminal and log to stderr. The default,
    // and what a service manager wants.
    foreground,
    // Detach and log to syslog.
    daemon
};

//
// Everything the daemon can be told from the command line.
//
struct options {
    // Loopback port the Evangelist extension posts frames to.
    int ingest_port = default_ingest_port;
    // Port Disciples connect to for the broadcast stream.
    int broadcast_port = default_broadcast_port;

    run_mode mode = run_mode::foreground;

    // Empty means no pid file is written. Opt in with --pid-file.
    std::string pid_file;

    log_level level = log_level::info;

    // Set when --log was given, which overrides the sink that the run
    // mode would otherwise imply.
    bool sink_forced = false;
    log_sink sink = log_sink::console;

    // Set by --help and --version, which print and exit.
    bool show_help = false;
    bool show_version = false;

    //
    // Where log lines should go, given the run mode and any override.
    //
    // Returns:
    //      The console when running in the foreground, syslog when
    //      detached, or whatever --log forced.
    //
    // Notes:
    //      A detached process has no stderr left, so defaulting it to
    //      the console would silently discard every line.
    //
    log_sink effective_sink() const;
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
//      argc, argv  - Arguments as handed to main().
//      opts        - Receives the parsed options. Untouched fields
//                    keep their defaults.
//      error       - Receives a message describing the problem when
//                    parsing fails.
//
// Returns:
//      True when the command line was understood.
//
// Sample call:
//      apostle::options opts;
//      std::string problem;
//      if (!apostle::parse_arguments(argc, argv, opts, problem)) {
//          std::cerr << "apostle: " << problem << "\n";
//      }
//
bool parse_arguments(int argc, char *argv[], options &opts,
                     std::string &error);

} // namespace apostle

#endif // APOSTLE_OPTIONS_H
