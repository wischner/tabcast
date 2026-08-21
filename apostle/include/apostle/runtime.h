//
// Process runtime for the Apostle daemon: detaching, the pid file, and
// signal handling.
//
// Everything here is about how the daemon sits in the operating
// system, not about what it does. It is deliberately separate from the
// eventual serving code, because these are the parts that have to be
// right before there is anything to serve.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_RUNTIME_H
#define APOSTLE_RUNTIME_H

#include <string>

namespace apostle {

//
// What a delivered signal means to the daemon.
//
enum class signal_event {
    // SIGTERM or SIGINT: shut down cleanly.
    stop,
    // SIGHUP: re-read configuration and carry on.
    reload
};

//
// Detach from the controlling terminal and become a background
// process.
//
// Returns:
//      True in the surviving daemon process. Never returns in the
//      intermediate processes, which exit. False when the detach
//      failed, in which case the caller still has its terminal and
//      can report the error.
//
// Notes:
//      The classic double fork. The first fork lets the shell's job
//      control return, setsid() starts a new session with no
//      controlling terminal, and the second fork leaves the process
//      unable to acquire one again by opening a tty. Standard input,
//      output, and error are replaced with /dev/null, so anything
//      that writes to them after this is discarded rather than
//      writing down a closed descriptor.
//
//      Under systemd you do not want this. A Type=simple unit expects
//      the program to stay in the foreground and reads its output
//      from the journal. See scripts/apostle.service.
//
bool daemonize();

//
// Take exclusive ownership of a pid file.
//
// Parameters:
//      path        - File to create, for example /run/apostle.pid.
//
// Returns:
//      True when this process now owns the file. False when another
//      live process already holds it, or the file cannot be written,
//      in which case errno describes why.
//
// Notes:
//      Ownership is an advisory lock held on the open descriptor, not
//      the existence of the file. That distinction matters: a file
//      left behind by a process that crashed carries no lock, so it
//      does not lock out the next start. Checking only for the file
//      would.
//
bool pid_file_acquire(const std::string &path);

//
// Release the pid file and unlink it.
//
// Notes:
//      Safe to call when no pid file was ever acquired.
//
void pid_file_release();

//
// Block the signals the daemon handles, so they can be collected
// deliberately rather than delivered as an interruption.
//
// Returns:
//      True on success.
//
// Notes:
//      Call this before starting any thread and before daemonizing.
//      The signal mask is inherited across fork and by every thread,
//      so blocking first is what makes wait_signal() the only place a
//      signal is ever seen. It also removes the whole class of bugs
//      that comes with async signal handlers, since no handler runs
//      at an arbitrary point in the middle of other work.
//
bool signals_block();

//
// Wait for one of the blocked signals and say what it means.
//
// Returns:
//      The meaning of the signal that arrived.
//
// Notes:
//      Blocks the calling thread. There is no race with a signal that
//      arrives just before the call, because a blocked signal stays
//      pending and is delivered by this function as soon as it asks.
//      A sleep-and-poll loop or a handler plus a flag both have that
//      race, this does not.
//
//      Use signal_open() instead once there are sockets to wait on
//      at the same time. This function is the same idea with nothing
//      else in the wait.
//
signal_event wait_signal();

//
// Open a descriptor that becomes readable when a blocked signal
// arrives.
//
// Returns:
//      The descriptor, or -1 with errno set.
//
// Notes:
//      This is what lets the daemon wait for a signal and for a
//      socket in one poll() rather than choosing between them. The
//      signals must already be blocked by signals_block(), which is
//      what keeps them pending for this descriptor instead of being
//      delivered as an interruption.
//
int signal_open();

//
// Collect one signal from a descriptor opened by signal_open().
//
// Parameters:
//      descriptor  - The descriptor, already reported readable.
//      event       - Receives the meaning of the signal.
//
// Returns:
//      True when a signal was read. False when there was nothing to
//      read, which a spurious wakeup can cause.
//
bool signal_next(int descriptor, signal_event &event);

//
// Close a descriptor opened by signal_open().
//
void signal_close(int descriptor);

} // namespace apostle

#endif // APOSTLE_RUNTIME_H
