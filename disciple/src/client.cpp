//
// The Disciple reader's entry logic: the platform objects, and the main
// loop.
//
// The loop is the one every inkerface program has: wait on the buttons
// for at most as long as the reader can spare, then let it do what is
// due. The reader checks its sockets with zero waits, so the only wait
// is on the buttons, and nothing needs a second thread.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/client.h"

#include "disciple/options.h"
#include "disciple/reader.h"
#include "disciple/screens.h"
#include "disciple/version.h"

#include <inkerface/display.h>
#include <inkerface/input.h>
#include <inkerface/network.h>
#include <inkerface/power.h>
#include <inkerface/storage.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace disciple {

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
    if (opts.show_version) {
        std::cout << "disciple " << version << " (protocol "
                  << protocol_version << ", inkerface)\n";
        return EXIT_SUCCESS;
    }

    // The input borrows the display, which on a desktop is what opens
    // inkerface's preview window when INKERFACE_PREVIEW=1.
    inkerface::display screen;
    inkerface::input buttons(screen);
    inkerface::network net;
    inkerface::storage disks;
    panel view(screen);

    reader_options settings;
    settings.host = opts.host;
    settings.port = opts.port;
    settings.wifi_setup = opts.wifi_setup;
    reader disciple(net, disks, view, settings, std::cout);

    const inkerface::size extent = view.extent();
    std::cout << "disciple " << version << "\n"
              << "  reader   " << disciple.name() << ", panel "
              << extent.width << "x" << extent.height << " at "
              << reader_gray_levels << " levels\n"
              << "  apostle  "
              << (opts.host.empty() ? "found by discovery, udp port " +
                                          std::to_string(opts.port)
                                    : "tcp://" + opts.host + ":" +
                                          std::to_string(opts.port))
              << "\n";

    disciple.start();
    while (!buttons.closed()) {
        disciple.step(buttons.poll_for(disciple.wait()));
        if (disciple.switched_off()) {
            // Does not return on a reader: the next power button press
            // boots it afresh. A desktop has nothing to switch off, and
            // the program simply ends.
            inkerface::power_off(screen);
            break;
        }
    }
    return EXIT_SUCCESS;
}

} // namespace disciple
