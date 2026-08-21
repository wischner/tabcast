//
// Command line parsing for the Disciple client.
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
// Parse a decimal integer within an inclusive range.
//
// Parameters:
//      text        - Argument text.
//      low, high   - Accepted range.
//      value       - Receives the parsed value on success.
//
// Returns:
//      True when the whole argument parsed and fell in range.
//
bool parse_int(std::string_view text, int low, int high, int &value) {
    int parsed = 0;
    const char *end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, parsed);

    if (result.ec != std::errc {} || result.ptr != end) {
        return false;
    }
    if (parsed < low || parsed > high) {
        return false;
    }
    value = parsed;
    return true;
}

//
// Fetch the value that follows an option.
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

bool options::is_replay() const {
    return !replay.empty();
}

void print_usage(std::ostream &out) {
    out << "usage: disciple [options]\n"
        << "\n"
        << "Tabcast e-ink client. Joins an Apostle daemon and renders "
           "the frames\n"
        << "it broadcasts, reduced to what the panel can actually "
           "show:\n"
        << "  " << panel_width << "x" << panel_height << " at "
        << panel_gray_levels << " grey levels.\n"
        << "\n"
        << "With --replay it renders a local image instead of "
           "connecting, which\n"
        << "shows exactly how a page will look on the reader without "
           "needing a\n"
        << "daemon or a device.\n"
        << "\n"
        << "options:\n"
        << "  -r, --replay FILE   render a local PNG or JPEG instead "
           "of connecting\n"
        << "  -s, --save FILE     write the rendered panel frame to a "
           "PNG\n"
        << "  -z, --zoom N        magnify the viewer 1 to " << max_zoom
        << " times (default 1)\n"
        << "  -n, --no-view       render without opening a window\n"
        << "      --host HOST     Apostle to join (default 127.0.0.1)\n"
        << "      --port PORT     broadcast port (default "
        << default_broadcast_port << ")\n"
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

        if (arg == "--replay" || arg == "-r") {
            if (!take_value(argc, argv, i, value)) {
                error = std::string(arg) + " needs a file";
                return false;
            }
            opts.replay.assign(value);
            continue;
        }
        if (arg == "--save" || arg == "-s") {
            if (!take_value(argc, argv, i, value)) {
                error = std::string(arg) + " needs a file";
                return false;
            }
            opts.save.assign(value);
            continue;
        }
        if (arg == "--zoom" || arg == "-z") {
            if (!take_value(argc, argv, i, value)) {
                error = std::string(arg) + " needs a number";
                return false;
            }
            if (!parse_int(value, 1, max_zoom, opts.zoom)) {
                error = std::string(value) + " is not a zoom between 1 and "
                        + std::to_string(max_zoom);
                return false;
            }
            continue;
        }
        if (arg == "--no-view" || arg == "-n") {
            opts.no_view = true;
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
            if (!parse_int(value, 1, 65535, opts.port)) {
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
