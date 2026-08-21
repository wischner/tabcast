//
// Logging for the Apostle daemon.
//
// One log call site works in both of the ways the daemon can run. In
// the foreground it writes a timestamped line to stderr, which is what
// a person watching a console wants and also what systemd captures
// into the journal. Detached with --daemon there is no terminal left
// to write to, so the same calls go to syslog instead.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_LOG_H
#define APOSTLE_LOG_H

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace apostle {

//
// Severity, ordered from most to least serious. Setting a level admits
// it and everything above it.
//
enum class log_level {
    error = 0,
    warn = 1,
    info = 2,
    debug = 3
};

//
// Where log lines go.
//
enum class log_sink {
    // Timestamped lines on stderr, for a console or the journal.
    console,
    // syslog(3), for a process with no terminal.
    system
};

//
// Parse a level name.
//
// Parameters:
//      name        - One of "error", "warn", "info", "debug".
//      level       - Receives the parsed value on success.
//
// Returns:
//      True when the name was recognised.
//
bool log_level_parse(std::string_view name, log_level &level);

//
// Name a level, for usage text and for the console prefix.
//
// Returns:
//      A static string such as "warn". Never null.
//
const char *log_level_name(log_level level);

//
// Start logging.
//
// Parameters:
//      ident       - Program name, used as the syslog identity.
//      level       - Most verbose level to admit.
//      sink        - Console or syslog.
//
// Notes:
//      Call after daemonizing, never before. Detaching closes stderr
//      and syslog's socket, so a connection opened first would be
//      writing into a closed descriptor.
//
void log_open(std::string_view ident, log_level level, log_sink sink);

//
// Stop logging and release the syslog connection.
//
void log_close();

//
// Test whether a level would be logged.
//
// Returns:
//      True when a message at this level would be written.
//
// Notes:
//      Exposed so log_write() can skip formatting work that would be
//      thrown away. Rarely worth calling directly.
//
bool log_enabled(log_level level);

//
// Write one already formatted message.
//
// Parameters:
//      level       - Severity.
//      text        - The message, without a trailing newline.
//
void log_message(log_level level, std::string_view text);

//
// Write one formatted message.
//
// Parameters:
//      level       - Severity.
//      fmt         - std::format style format string, checked at
//                    compile time.
//      args        - Values for the format string.
//
// Sample call:
//      log_write(log_level::info, "listening on port {}", port);
//
// Notes:
//      This is a template, so unlike every other function here its
//      body has to live in the header. It does as little as possible:
//      drop the call early if the level is off, otherwise format and
//      hand the result to log_message() in the .cpp file.
//
template <typename... arg_types>
void log_write(log_level level,
               std::format_string<arg_types...> fmt,
               arg_types &&...args) {
    if (!log_enabled(level)) {
        return;
    }
    log_message(level,
                std::format(fmt, std::forward<arg_types>(args)...));
}

} // namespace apostle

#endif // APOSTLE_LOG_H
