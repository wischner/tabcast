//
// Logging implementation for the Apostle daemon.
//
// Holds the one piece of global state the daemon has: where log lines
// go and how verbose they are. It is global because logging is called
// from everywhere and threading a logger through every function would
// buy nothing here.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/log.h"

#include <cstdio>
#include <ctime>
#include <syslog.h>

namespace apostle {
namespace {

// Current settings. Written once at startup by log_open().
log_level current_level = log_level::info;
log_sink current_sink = log_sink::console;
bool syslog_open = false;

// Kept alive for syslog(3), which stores the pointer it is given
// rather than copying the string.
std::string syslog_ident;

//
// Map our level onto a syslog priority.
//
int syslog_priority(log_level level) {
    switch (level) {
    case log_level::error:
        return LOG_ERR;
    case log_level::warn:
        return LOG_WARNING;
    case log_level::debug:
        return LOG_DEBUG;
    case log_level::info:
    default:
        return LOG_INFO;
    }
}

//
// Format the local time as "YYYY-MM-DD HH:MM:SS".
//
// Parameters:
//      out         - Buffer of at least 20 bytes.
//      size        - Size of that buffer.
//
// Notes:
//      Uses localtime_r and strftime rather than the chrono
//      formatters, because the chrono time zone support needs a
//      system time zone database that a stripped down container may
//      not carry. A log line must never be the thing that fails.
//
void format_now(char *out, std::size_t size) {
    const std::time_t now = std::time(nullptr);
    std::tm parts {};

    if (localtime_r(&now, &parts) == nullptr) {
        std::snprintf(out, size, "%s", "0000-00-00 00:00:00");
        return;
    }
    if (std::strftime(out, size, "%Y-%m-%d %H:%M:%S", &parts) == 0) {
        std::snprintf(out, size, "%s", "0000-00-00 00:00:00");
    }
}

} // namespace

bool log_level_parse(std::string_view name, log_level &level) {
    if (name == "error") {
        level = log_level::error;
    } else if (name == "warn") {
        level = log_level::warn;
    } else if (name == "info") {
        level = log_level::info;
    } else if (name == "debug") {
        level = log_level::debug;
    } else {
        return false;
    }
    return true;
}

const char *log_level_name(log_level level) {
    switch (level) {
    case log_level::error:
        return "error";
    case log_level::warn:
        return "warn";
    case log_level::debug:
        return "debug";
    case log_level::info:
    default:
        return "info";
    }
}

void log_open(std::string_view ident, log_level level, log_sink sink) {
    log_close();

    current_level = level;
    current_sink = sink;

    if (sink == log_sink::system) {
        syslog_ident.assign(ident);
        openlog(syslog_ident.c_str(), LOG_PID, LOG_DAEMON);
        syslog_open = true;
    }
}

void log_close() {
    if (syslog_open) {
        closelog();
        syslog_open = false;
    }
}

bool log_enabled(log_level level) {
    return static_cast<int>(level) <= static_cast<int>(current_level);
}

void log_message(log_level level, std::string_view text) {
    if (!log_enabled(level)) {
        return;
    }

    if (current_sink == log_sink::system) {
        // syslog stamps and routes the line itself.
        syslog(syslog_priority(level), "%.*s",
               static_cast<int>(text.size()), text.data());
        return;
    }

    char stamp[24];
    format_now(stamp, sizeof stamp);

    // One fprintf, so two threads cannot interleave halves of a line.
    std::fprintf(stderr, "%s %-5s %.*s\n", stamp, log_level_name(level),
                 static_cast<int>(text.size()), text.data());
    std::fflush(stderr);
}

} // namespace apostle
