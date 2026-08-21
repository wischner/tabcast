//
// Process runtime implementation for the Apostle daemon.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/runtime.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace apostle {
namespace {

// Open descriptor of the pid file, and the path to unlink on release.
// The descriptor is held open for the life of the process because
// closing it would drop the lock that proves we are the live instance.
int pid_fd = -1;
std::string pid_path;

// Permissions for a new pid file: readable by anyone, writable only by
// the owner.
constexpr mode_t pid_file_mode = 0644;

// Files the daemon creates are not for the world to read.
constexpr mode_t daemon_umask = 0027;

//
// Replace a standard descriptor with /dev/null.
//
// Returns:
//      True when all three of stdin, stdout, and stderr were replaced.
//
bool silence_standard_streams() {
    const int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        return false;
    }

    const bool ok = dup2(null_fd, STDIN_FILENO) >= 0 &&
                    dup2(null_fd, STDOUT_FILENO) >= 0 &&
                    dup2(null_fd, STDERR_FILENO) >= 0;

    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }
    return ok;
}

//
// Fork and exit the parent, leaving only the child running.
//
// Returns:
//      True in the child. False when the fork failed.
//
bool fork_and_leave_parent() {
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        // _exit, not exit: the parent must not run atexit handlers or
        // flush buffers that the child also holds a copy of.
        _exit(EXIT_SUCCESS);
    }
    return true;
}

} // namespace

bool daemonize() {
    // First fork: the shell gets its prompt back, and the child is
    // guaranteed not to be a process group leader, which setsid needs.
    if (!fork_and_leave_parent()) {
        return false;
    }
    if (setsid() < 0) {
        return false;
    }
    // Second fork: the result is not a session leader, so it can never
    // acquire a controlling terminal by opening a tty.
    if (!fork_and_leave_parent()) {
        return false;
    }

    umask(daemon_umask);

    // Do not hold a directory open that someone may want to unmount.
    if (chdir("/") < 0) {
        return false;
    }
    return silence_standard_streams();
}

bool pid_file_acquire(const std::string &path) {
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC,
                        pid_file_mode);
    if (fd < 0) {
        return false;
    }

    // The lock, not the file, is what says an instance is alive. A
    // file left behind by a crash holds no lock and does not block a
    // restart.
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return false;
    }

    // Truncate before writing, in case a stale file held a longer pid.
    if (ftruncate(fd, 0) < 0) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return false;
    }

    char text[32];
    const int length = std::snprintf(text, sizeof text, "%ld\n",
                                     static_cast<long>(getpid()));
    if (length <= 0 || write(fd, text, static_cast<size_t>(length)) != length) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return false;
    }

    pid_fd = fd;
    pid_path = path;
    return true;
}

void pid_file_release() {
    if (pid_fd < 0) {
        return;
    }
    // Unlink first, then close. Closing drops the lock, and doing that
    // first would leave a window where another instance could take the
    // lock on a file this process is about to delete.
    unlink(pid_path.c_str());
    close(pid_fd);

    pid_fd = -1;
    pid_path.clear();
}

bool signals_block() {
    sigset_t mask;

    if (sigemptyset(&mask) < 0) {
        return false;
    }
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);

    // Ignore SIGPIPE outright. A reader that disappears mid frame must
    // surface as a failed write on that one connection, not as the
    // death of the whole daemon.
    signal(SIGPIPE, SIG_IGN);

    return sigprocmask(SIG_BLOCK, &mask, nullptr) == 0;
}

signal_event wait_signal() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);

    for (;;) {
        int received = 0;
        const int status = sigwait(&mask, &received);

        if (status != 0) {
            // The only documented failure is an invalid mask, which
            // cannot happen here. Treat anything unexpected as a
            // reason to shut down rather than spin.
            return signal_event::stop;
        }
        if (received == SIGHUP) {
            return signal_event::reload;
        }
        if (received == SIGTERM || received == SIGINT) {
            return signal_event::stop;
        }
    }
}

} // namespace apostle

//
// The set of signals the daemon collects deliberately. Kept beside
// signals_block(), which is what makes them pending rather than
// delivered.
//
namespace apostle {
namespace {

//
// Fill a mask with the signals the daemon handles.
//
bool handled_signals(sigset_t &mask) {
    if (sigemptyset(&mask) < 0) {
        return false;
    }
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    return true;
}

} // namespace

int signal_open() {
    sigset_t mask;
    if (!handled_signals(mask)) {
        return -1;
    }
    return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

bool signal_next(int descriptor, signal_event &event) {
    signalfd_siginfo info{};
    const ssize_t got = read(descriptor, &info, sizeof(info));

    if (got != static_cast<ssize_t>(sizeof(info))) {
        return false;
    }

    event = info.ssi_signo == SIGHUP ? signal_event::reload
                                     : signal_event::stop;
    return true;
}

void signal_close(int descriptor) {
    if (descriptor >= 0) {
        close(descriptor);
    }
}

} // namespace apostle
