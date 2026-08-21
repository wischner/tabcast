//
// Command line parsing for the Apostle daemon.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/options.h"

#include <charconv>
#include <ostream>
#include <string_view>

namespace apostle {
namespace {

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
// Fetch the value that follows an option.
//
// Parameters:
//      argc, argv  - The command line.
//      index       - Index of the option itself. Advanced past the
//                    value on success.
//      value       - Receives the value.
//
// Returns:
//      True when a value was present.
//
bool take_value(int argc, char *argv[], int &index,
                std::string_view &value) {
    if (index + 1 >= argc) {
        return false;
    }
    value = argv[++index];
    return true;
}

} // namespace

log_sink options::effective_sink() const {
    if (sink_forced) {
        return sink;
    }
    return mode == run_mode::daemon ? log_sink::system
                                    : log_sink::console;
}

void print_usage(std::ostream &out) {
    out << "usage: apostle [options]\n"
        << "\n"
        << "Tabcast broadcast daemon. Accepts frames from the "
           "Evangelist browser\n"
        << "extension and distributes them to every registered "
           "Disciple.\n"
        << "\n"
        << "Runs in the foreground by default, logging to stderr. That "
           "is what a\n"
        << "console and what a service manager both want. Use --daemon "
           "only when\n"
        << "you want it detached with no supervisor.\n"
        << "\n"
        << "options:\n"
        << "  --ingest-port PORT     loopback port Evangelist posts "
           "frames to\n"
        << "                         (default " << default_ingest_port
        << ")\n"
        << "  --broadcast-port PORT  port Disciples connect to\n"
        << "                         (default " << default_broadcast_port
        << ")\n"
        << "  -d, --daemon           detach from the terminal and log "
           "to syslog\n"
        << "  -f, --foreground       stay attached and log to stderr "
           "(default)\n"
        << "  -p, --pid-file PATH    write and lock a pid file, for "
           "example\n"
        << "                         /run/apostle.pid. Not needed "
           "under systemd\n"
        << "  -l, --log-level LEVEL  error, warn, info, or debug "
           "(default info)\n"
        << "      --log WHERE        console or syslog, overriding "
           "what the\n"
        << "                         run mode implies\n"
        << "      --version          print the version and exit\n"
        << "  -h, --help             print this text and exit\n";
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
        if (arg == "--daemon" || arg == "-d") {
            opts.mode = run_mode::daemon;
            continue;
        }
        if (arg == "--foreground" || arg == "-f") {
            opts.mode = run_mode::foreground;
            continue;
        }

        if (arg == "--ingest-port" || arg == "--broadcast-port") {
            if (!take_value(argc, argv, i, value)) {
                error = std::string(arg) + " needs a port number";
                return false;
            }
            int &target = arg == "--ingest-port" ? opts.ingest_port
                                                 : opts.broadcast_port;
            if (!parse_port(value, target)) {
                error = std::string(value) + " is not a valid port";
                return false;
            }
            continue;
        }

        if (arg == "--pid-file" || arg == "-p") {
            if (!take_value(argc, argv, i, value)) {
                error = std::string(arg) + " needs a path";
                return false;
            }
            opts.pid_file.assign(value);
            continue;
        }

        if (arg == "--log-level" || arg == "-l") {
            if (!take_value(argc, argv, i, value)) {
                error = std::string(arg) + " needs a level";
                return false;
            }
            if (!log_level_parse(value, opts.level)) {
                error = std::string(value) +
                        " is not a level, expected error, warn, info, "
                        "or debug";
                return false;
            }
            continue;
        }

        if (arg == "--log") {
            if (!take_value(argc, argv, i, value)) {
                error = "--log needs console or syslog";
                return false;
            }
            if (value == "console") {
                opts.sink = log_sink::console;
            } else if (value == "syslog") {
                opts.sink = log_sink::system;
            } else {
                error = std::string(value) +
                        " is not a log destination, expected console "
                        "or syslog";
                return false;
            }
            opts.sink_forced = true;
            continue;
        }

        error = "unknown option " + std::string(arg);
        return false;
    }
    return true;
}

} // namespace apostle
