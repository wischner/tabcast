//
// Apostle entry point.
//
// Brings the daemon up in whichever way it was asked to run, then
// waits. The order of the startup steps matters and is the main thing
// this file is responsible for getting right:
//
//   1. Parse the command line, while a terminal is still attached to
//      complain to.
//   2. Block signals, before any fork, so every later process and
//      thread inherits the mask.
//   3. Detach, if asked. This closes stderr.
//   4. Only now open the log, because step 3 decides where it can go.
//   5. Take the pid file, so a second instance fails here rather than
//      halfway through binding a port.
//
//   6. Bind both listeners, and only then start serving.
//
// After that the daemon is one poll loop over a signalfd, the two
// listeners, and every connection either of them holds. A frame
// posted by Evangelist is stored and fanned out to every registered
// Disciple in the same turn of that loop.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/broadcast_server.h"
#include "apostle/frame_store.h"
#include "apostle/http_server.h"
#include "apostle/log.h"
#include "apostle/options.h"
#include "apostle/runtime.h"
#include "apostle/version.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include <poll.h>

namespace {

using namespace apostle;

//
// Log what this instance is and what it would serve.
//
void log_banner(const options &opts) {
    log_write(log_level::info, "apostle {} starting, protocol {}",
              version, protocol_version);
    log_write(log_level::info, "ingest    http://127.0.0.1:{}/api/v1/frame",
              opts.ingest_port);
    log_write(log_level::info, "broadcast tcp/*:{}", opts.broadcast_port);
    log_write(log_level::debug, "log level {}, running in {}",
              log_level_name(opts.level),
              opts.mode == run_mode::daemon ? "the background"
                                            : "the foreground");
}

//
// Serve until a signal says to stop.
//
// Parameters:
//      ingest      - The ingest listener, already open.
//      broadcast   - The broadcast listener, already open.
//
// Returns:
//      True on a clean shutdown, false when the wait itself broke.
//
// Notes:
//      Signals and sockets are waited on together, through a
//      signalfd in the same poll set. Handling them in one place is
//      what makes shutdown ordinary: a SIGTERM arriving mid request
//      is just another readable descriptor, not an interruption of
//      whatever was running.
//
//      SIGHUP will re-read configuration once there is any. Today it
//      only logs, which is still worth having: it proves the signal
//      path works before anything depends on it.
//
bool run_until_stopped(http_server &ingest,
                       broadcast_server &broadcast) {
    const int signals = signal_open();
    if (signals < 0) {
        log_write(log_level::error, "cannot watch for signals: {}",
                  std::strerror(errno));
        return false;
    }

    std::vector<pollfd> waiting;
    bool stopping = false;

    while (!stopping) {
        waiting.clear();
        waiting.push_back(pollfd{signals, POLLIN, 0});
        ingest.watch(waiting);
        broadcast.watch(waiting);

        // A timeout rather than an indefinite wait, so keepalive
        // still runs on a daemon that nothing is talking to. That is
        // precisely when a dead reader needs noticing.
        const int ready = poll(waiting.data(), waiting.size(), 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_write(log_level::error, "poll failed: {}",
                      std::strerror(errno));
            signal_close(signals);
            return false;
        }

        // Taken before dispatching, because handling a descriptor
        // can close another one in the same sweep.
        const std::vector<pollfd> reported = waiting;

        for (const pollfd &entry : reported) {
            if (entry.revents == 0) {
                continue;
            }

            if (entry.fd == signals) {
                signal_event event = signal_event::stop;
                if (!signal_next(signals, event)) {
                    continue;
                }
                if (event == signal_event::stop) {
                    stopping = true;
                    break;
                }
                log_write(log_level::info,
                          "SIGHUP received, nothing to reload yet");
                continue;
            }

            // Each listener ignores a descriptor that is not its
            // own, and one it has already closed, so both get to see
            // everything poll() reported.
            ingest.ready(entry.fd);
            broadcast.ready(entry.fd, entry.revents);
        }

        broadcast.tick();
    }

    signal_close(signals);
    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    options opts;
    std::string problem;

    if (!parse_arguments(argc, argv, opts, problem)) {
        std::cerr << "apostle: " << problem << "\n";
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }
    if (opts.show_help) {
        print_usage(std::cout);
        return EXIT_SUCCESS;
    }
    if (opts.show_version) {
        std::cout << "apostle " << version << " (protocol "
                  << protocol_version << ")\n";
        return EXIT_SUCCESS;
    }

    // Before any fork, so the mask is inherited rather than raced for.
    if (!signals_block()) {
        std::cerr << "apostle: cannot block signals: "
                  << std::strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    // The last point at which an error can reach a terminal.
    if (opts.mode == run_mode::daemon && !daemonize()) {
        std::cerr << "apostle: cannot detach: " << std::strerror(errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    log_open("apostle", opts.level, opts.effective_sink());

    if (!opts.pid_file.empty() && !pid_file_acquire(opts.pid_file)) {
        log_write(log_level::error, "cannot take pid file {}: {}",
                  opts.pid_file, std::strerror(errno));
        log_write(log_level::error,
                  "another apostle may already be running");
        log_close();
        return EXIT_FAILURE;
    }

    log_banner(opts);

    frame_store frames;
    http_server ingest(frames);
    broadcast_server broadcast;
    std::string failure;

    if (!ingest.open(opts.ingest_port, failure)) {
        log_write(log_level::error, "{}", failure);
        log_write(log_level::error,
                  "another apostle may already be running");
        pid_file_release();
        log_close();
        return EXIT_FAILURE;
    }

    if (!broadcast.open(opts.broadcast_port, failure)) {
        log_write(log_level::error, "{}", failure);
        ingest.close();
        pid_file_release();
        log_close();
        return EXIT_FAILURE;
    }

    // The two halves are joined here and nowhere else. Neither
    // listener knows the other's type, which is what keeps the ingest
    // path testable without a flock and the fan-out testable without
    // a browser.
    ingest.on_frame([&broadcast](const frame &value) {
        broadcast.publish(value);
    });
    ingest.client_counter([&broadcast]() {
        return broadcast.client_count();
    });

    log_write(log_level::info, "listening, waiting for disciples");

    const bool clean = run_until_stopped(ingest, broadcast);

    log_write(log_level::info, "shutting down after {} frames",
              frames.accepted());
    broadcast.close();
    ingest.close();
    pid_file_release();
    log_close();
    return clean ? EXIT_SUCCESS : EXIT_FAILURE;
}
