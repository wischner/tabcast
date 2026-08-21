//
// Disciple client.
//
// Two things it can do today. `--replay FILE` runs the whole render
// path over a local image: decode, fit to the panel, reduce to the
// panel's grey levels, and show or save the result. Without it, it
// reports what it would connect to and stops, because the broadcast
// protocol is not written yet.
//
// The replay path is not a toy. It is the same code that will render a
// received frame, so a page that reads badly here will read badly on
// the reader, and that can be found out now rather than after the
// hardware arrives.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/client.h"

#include "disciple/backend.h"
#include "disciple/net.h"
#include "disciple/options.h"
#include "disciple/panel.h"
#include "disciple/version.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace disciple {
namespace {

//
// Read a whole file into memory.
//
// Parameters:
//      path        - File to read.
//      bytes       - Receives the contents.
//      error       - Receives a message when the read fails.
//
// Returns:
//      True on success.
//
// Notes:
//      Replay deliberately loads the file into a buffer and decodes
//      from memory, rather than decoding from the path. That is the
//      shape a frame arrives in off the socket, so replay exercises
//      the real path instead of a convenient one.
//
bool read_file(const std::string &path, std::vector<std::uint8_t> &bytes,
               std::string &error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }

    bytes.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        error = path + " is empty";
        return false;
    }
    return true;
}

//
// Describe this build and its target panel.
//
void print_banner(const backend &display, const options &opts) {
    std::cout << "disciple " << version << "\n"
              << "  device   " << device_name << " " << panel_width
              << "x" << panel_height << ", " << panel_gray_levels
              << " grey levels (" << panel_gray_bits() << " bpp, "
              << panel_frame_bytes() << " bytes a frame)\n"
              << "  backend  " << display.name() << "\n";

    if (!opts.is_replay()) {
        std::cout << "  apostle  tcp://" << opts.host << ":"
                  << opts.port << "\n";
    }
}

//
// Render one image file onto the panel, then show it, save it, or
// both.
//
// Returns:
//      A process exit code.
//
int replay(backend &display, const options &opts) {
    std::vector<std::uint8_t> encoded;
    std::string error;

    if (!read_file(opts.replay, encoded, error)) {
        std::cerr << "disciple: " << error << "\n";
        return EXIT_FAILURE;
    }

    rgba_image source;
    if (!display.decode(encoded.data(), encoded.size(), source, error)) {
        std::cerr << "disciple: cannot decode " << opts.replay << ": "
                  << error << "\n";
        return EXIT_FAILURE;
    }

    const panel_frame frame =
        render_to_panel(source.pixels.data(), source.width, source.height);

    std::cout << "  source   " << opts.replay << ", " << source.width
              << "x" << source.height << "\n"
              << "  rendered " << panel_frame::width << "x"
              << panel_frame::height << ", packs to "
              << frame.pack().size() << " bytes\n";

    if (!opts.save.empty()) {
        if (!display.save(frame, opts.save, error)) {
            std::cerr << "disciple: cannot save " << opts.save << ": "
                      << error << "\n";
            return EXIT_FAILURE;
        }
        std::cout << "  saved    " << opts.save << "\n";
    }

    if (opts.no_view) {
        return EXIT_SUCCESS;
    }
    if (!display.can_display()) {
        if (opts.save.empty()) {
            std::cerr << "disciple: this build cannot display anything, "
                         "use --save, or rebuild with BACKEND=test\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    return display.view(frame, opts.zoom);
}

//
// Join an Apostle and show what it broadcasts.
//
// Returns:
//      A process exit code.
//
// Notes:
//      The socket is handed to the display backend, which watches it
//      from the toolkit's own event loop. There is no second thread
//      and no polling: a frame arrives, it is rendered through the
//      same path --replay uses, and the window is redrawn.
//
int stream(backend &display, const options &opts) {
    connection link;
    std::string error;

    if (!link.open(opts.host, opts.port, error)) {
        std::cerr << "disciple: " << error << "\n";
        return EXIT_FAILURE;
    }
    if (!link.handshake(error)) {
        std::cerr << "disciple: " << error << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "  joined   as " << link.client_id() << "\n";

    if (!display.can_display()) {
        std::cerr << "disciple: this build cannot display anything, "
                     "rebuild with BACKEND=test\n";
        return EXIT_FAILURE;
    }

    bool failed = false;

    const int status = display.watch(
        link.descriptor(),
        [&](live_view &view) {
            std::vector<received_frame> arrived;
            std::string problem;

            if (!link.read_ready(arrived, problem)) {
                if (!problem.empty()) {
                    std::cerr << "disciple: " << problem << "\n";
                    failed = true;
                } else {
                    std::cout << "  apostle closed the connection\n";
                }
                return false;
            }

            for (const received_frame &received : arrived) {
                const auto started =
                    std::chrono::steady_clock::now();

                rgba_image source;
                if (!display.decode(received.image.data(),
                                    received.image.size(), source,
                                    problem)) {
                    // One unreadable frame is not a reason to leave.
                    // The next one is very likely fine, and a client
                    // that quits here is a client that quits during
                    // a broadcast.
                    std::cerr << "disciple: cannot decode frame "
                              << received.header.sequence << ": "
                              << problem << "\n";
                    continue;
                }

                view.show(render_to_panel(source.pixels.data(),
                                          source.width,
                                          source.height));

                const auto elapsed =
                    std::chrono::steady_clock::now() - started;
                link.acknowledge(
                    received.header.sequence,
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(elapsed)
                        .count());
            }
            return true;
        },
        opts.zoom);

    link.close();
    return failed ? EXIT_FAILURE : status;
}

} // namespace

int run(int argc, char *argv[]) {
    options opts;
    std::string problem;

    if (!parse_arguments(argc, argv, opts, problem)) {
        std::cerr << "disciple: " << problem << "\n";
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }
    if (opts.show_help) {
        print_usage(std::cout);
        return EXIT_SUCCESS;
    }

    const std::unique_ptr<backend> display = make_backend();

    if (opts.show_version) {
        std::cout << "disciple " << version << " (protocol "
                  << protocol_version << ", " << display->name() << ")\n";
        return EXIT_SUCCESS;
    }

    print_banner(*display, opts);

    if (opts.is_replay()) {
        return replay(*display, opts);
    }
    return stream(*display, opts);
}

} // namespace disciple
