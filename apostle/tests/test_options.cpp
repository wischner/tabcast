//
// Tests for the Apostle command line parser.
//
// The parser is worth testing on its own because it decides how the
// daemon runs before anything else happens, and because a mistake here
// shows up as a daemon that logs into a closed descriptor or refuses
// to start, both of which are tedious to debug from the outside.
//
// No test framework. There is no package manager in this project, so
// the harness is a check macro, a counter, and an exit code.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/options.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

//
// Report one assertion.
//
// Parameters:
//      passed      - Result of the condition.
//      what        - Description, printed either way so a passing run
//                    still shows what was covered.
//
void check(bool passed, const char *what) {
    std::printf("%s  %s\n", passed ? "PASS" : "FAIL", what);
    if (!passed) {
        ++failures;
    }
}

//
// Run the parser over a command line written as it would be typed.
//
// Parameters:
//      args        - Arguments after the program name.
//      opts        - Receives the parsed options.
//      error       - Receives the failure message, if any.
//
// Returns:
//      What parse_arguments() returned.
//
// Notes:
//      parse_arguments takes char *argv[], matching main(), so the
//      strings have to be laid out as a mutable array of pointers.
//
bool parse(std::vector<std::string> args, apostle::options &opts,
           std::string &error) {
    std::vector<std::string> owned;
    owned.push_back("apostle");
    for (auto &arg : args) {
        owned.push_back(arg);
    }

    std::vector<char *> argv;
    for (auto &arg : owned) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    return apostle::parse_arguments(static_cast<int>(owned.size()),
                                    argv.data(), opts, error);
}

//
// The defaults are part of the contract, so they are asserted rather
// than assumed.
//
void test_defaults() {
    apostle::options opts;
    std::string error;

    check(parse({}, opts, error), "empty command line parses");
    check(opts.mode == apostle::run_mode::foreground,
          "defaults to the foreground");
    check(opts.ingest_port == apostle::default_ingest_port,
          "defaults to the standard ingest port");
    check(opts.broadcast_port == apostle::default_broadcast_port,
          "defaults to the standard broadcast port");
    check(opts.level == apostle::log_level::info,
          "defaults to info logging");
    check(opts.pid_file.empty(), "writes no pid file unless asked");
}

//
// The run mode decides where the log can go. Getting this wrong sends
// a detached daemon's output to a closed stderr.
//
void test_sink_follows_mode() {
    apostle::options opts;
    std::string error;

    check(parse({"--foreground"}, opts, error) &&
              opts.effective_sink() == apostle::log_sink::console,
          "foreground logs to the console");

    opts = apostle::options {};
    check(parse({"--daemon"}, opts, error) &&
              opts.effective_sink() == apostle::log_sink::system,
          "daemon logs to syslog");

    opts = apostle::options {};
    check(parse({"--daemon", "--log", "console"}, opts, error) &&
              opts.effective_sink() == apostle::log_sink::console,
          "--log overrides what the mode implies");

    opts = apostle::options {};
    check(parse({"--daemon", "--foreground"}, opts, error) &&
              opts.mode == apostle::run_mode::foreground,
          "the last mode flag on the line wins");
}

//
// Values that follow an option.
//
void test_values() {
    apostle::options opts;
    std::string error;

    check(parse({"--ingest-port", "9000", "--broadcast-port", "9001"},
                opts, error) &&
              opts.ingest_port == 9000 && opts.broadcast_port == 9001,
          "both ports are read");

    opts = apostle::options {};
    check(parse({"-p", "/run/apostle.pid"}, opts, error) &&
              opts.pid_file == "/run/apostle.pid",
          "short -p sets the pid file");

    opts = apostle::options {};
    check(parse({"-l", "debug"}, opts, error) &&
              opts.level == apostle::log_level::debug,
          "short -l sets the log level");

    opts = apostle::options {};
    check(parse({"--help"}, opts, error) && opts.show_help,
          "--help is recognised");

    opts = apostle::options {};
    check(parse({"--version"}, opts, error) && opts.show_version,
          "--version is recognised");
}

//
// Bad input must be refused with a message, never accepted quietly. A
// port silently clamped or ignored is worse than a startup failure.
//
void test_rejections() {
    apostle::options opts;
    std::string error;

    check(!parse({"--ingest-port", "0"}, opts, error),
          "port 0 is refused");
    check(!parse({"--ingest-port", "65536"}, opts, error),
          "port 65536 is refused");
    check(!parse({"--ingest-port", "80x"}, opts, error),
          "a port with trailing junk is refused");
    check(!parse({"--ingest-port"}, opts, error),
          "a port option with no value is refused");
    check(!parse({"--log-level", "chatty"}, opts, error),
          "an unknown log level is refused");
    check(!parse({"--log", "teletype"}, opts, error),
          "an unknown log destination is refused");
    check(!parse({"--broadcast"}, opts, error),
          "an unknown option is refused");

    error.clear();
    check(!parse({"--nonsense"}, opts, error) && !error.empty(),
          "a refusal comes with a message");
}

} // namespace

int main() {
    test_defaults();
    test_sink_follows_mode();
    test_values();
    test_rejections();

    std::printf("\n%s\n", failures == 0 ? "all checks passed"
                                        : "there were failures");
    return failures == 0 ? 0 : 1;
}
