//
// Tests for the HTTP request parser.
//
// Everything here is fed from string literals, because the parser has
// no I/O in it. That is deliberate: the decisions about what counts as
// a valid frame upload are the ones worth pinning down, and they need
// neither a socket nor a browser to check.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/http.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

//
// Report one check.
//
void check(bool passed, const std::string &what) {
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << "\n";
    if (!passed) {
        ++failures;
    }
}

//
// Feed a whole request to a parser in one go.
//
apostle::parse_result feed_all(apostle::request_parser &parser,
                               std::string_view text) {
    return parser.feed(reinterpret_cast<const std::uint8_t *>(
                           text.data()),
                       text.size());
}

//
// A complete, valid frame upload.
//
void test_frame_post() {
    apostle::request_parser parser(1024);
    const std::string request =
        "POST /api/v1/frame HTTP/1.1\r\n"
        "Host: 127.0.0.1:8420\r\n"
        "Content-Type: image/png\r\n"
        "Content-Length: 4\r\n"
        "X-Tabcast-Sequence: 42\r\n"
        "X-Tabcast-Session: abc123\r\n"
        "X-Tabcast-Width: 1200\r\n"
        "X-Tabcast-Height: 720\r\n"
        "\r\n"
        "\x89PNG";

    check(feed_all(parser, request) == apostle::parse_result::done,
          "a complete POST parses");

    const apostle::http_request &parsed = parser.request();
    check(parsed.method == "POST", "the method is POST");
    check(parsed.target == "/api/v1/frame", "the target is the frame "
                                            "endpoint");
    check(parsed.body.size() == 4, "the body is the declared length");

    const std::string *type = parsed.header("Content-Type");
    check(type != nullptr && *type == "image/png",
          "Content-Type is read");

    const std::string *sequence = parsed.header("x-tabcast-SEQUENCE");
    check(sequence != nullptr && *sequence == "42",
          "header lookup ignores case");

    check(parsed.header("X-Tabcast-Title") == nullptr,
          "an absent header reads as absent");
}

//
// Bytes arriving in pieces, which is how a socket delivers them.
//
void test_split_delivery() {
    apostle::request_parser parser(1024);
    const std::string head =
        "POST /api/v1/frame HTTP/1.1\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 6\r\n"
        "X-Tabcast-Session: s\r\n"
        "\r\n";

    check(feed_all(parser, head.substr(0, 20)) ==
              apostle::parse_result::need_more,
          "half a request line is not a request");
    check(feed_all(parser, head.substr(20)) ==
              apostle::parse_result::need_more,
          "a head with no body yet is not a request");
    check(feed_all(parser, "abc") == apostle::parse_result::need_more,
          "half a body is not a request");
    check(feed_all(parser, "def") == apostle::parse_result::done,
          "the last body byte completes it");
    check(parser.request().body.size() == 6,
          "the body is reassembled whole");
}

//
// A GET with no body at all.
//
void test_health_get() {
    apostle::request_parser parser(1024);
    check(feed_all(parser, "GET /api/v1/health HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n\r\n") ==
              apostle::parse_result::done,
          "a GET with no body completes at the blank line");
    check(parser.request().body.empty(), "and carries no body");
}

//
// The refusals.
//
void test_refusals() {
    {
        apostle::request_parser parser(16);
        check(feed_all(parser, "POST /api/v1/frame HTTP/1.1\r\n"
                               "Content-Length: 64\r\n\r\n") ==
                  apostle::parse_result::failed,
              "a body over the limit is refused");
        check(parser.status() == 413, "and refused with 413");
    }
    {
        apostle::request_parser parser(1024);
        check(feed_all(parser, "POST /api/v1/frame HTTP/1.1\r\n"
                               "Content-Type: image/png\r\n\r\n") ==
                  apostle::parse_result::failed,
              "a POST without Content-Length is refused");
        check(parser.status() == 400, "and refused with 400");
    }
    {
        apostle::request_parser parser(1024);
        check(feed_all(parser, "POST /api/v1/frame HTTP/1.1\r\n"
                               "Content-Length: twelve\r\n\r\n") ==
                  apostle::parse_result::failed,
              "a non-numeric Content-Length is refused");
    }
    {
        apostle::request_parser parser(1024);
        check(feed_all(parser, "POST /api/v1/frame HTTP/1.1\r\n"
                               "Transfer-Encoding: chunked\r\n\r\n") ==
                  apostle::parse_result::failed,
              "a chunked body is refused rather than half handled");
    }
    {
        apostle::request_parser parser(1024);
        check(feed_all(parser, "GARBAGE\r\n\r\n") ==
                  apostle::parse_result::failed,
              "a malformed request line is refused");
    }
    {
        apostle::request_parser parser(1024);
        check(feed_all(parser, "GET / HTTP/1.1\r\n"
                               "Content-Length: 0\r\n"
                               " folded: yes\r\n\r\n") ==
                  apostle::parse_result::failed,
              "an obsolete folded header is refused");
    }
}

//
// Percent decoding, which titles and URLs arrive in.
//
void test_percent_decode() {
    check(apostle::percent_decode("plain") == "plain",
          "text with no escapes is unchanged");
    check(apostle::percent_decode("a%20b") == "a b",
          "an escape decodes");
    // Split so the hex escape cannot swallow the letter after it:
    // "\x8de" would be one out-of-range escape, not \x8d then 'e'.
    check(apostle::percent_decode("%C4%8Develjust") ==
              "\xc4\x8d"
              "eveljust",
          "a multi-byte UTF-8 sequence survives");
    check(apostle::percent_decode("100%") == "100%",
          "a trailing percent is left alone");
    check(apostle::percent_decode("%zz") == "%zz",
          "a malformed escape is left alone rather than dropped");
}

//
// Number parsing, which every required header goes through.
//
void test_parse_number() {
    long long value = -1;
    check(apostle::parse_number("42", value) && value == 42,
          "digits parse");
    check(apostle::parse_number("  7  ", value) && value == 7,
          "surrounding spaces are ignored");
    check(!apostle::parse_number("", value), "an empty value fails");
    check(!apostle::parse_number("-1", value), "a negative value fails");
    check(!apostle::parse_number("12x", value), "trailing rubbish fails");
}

} // namespace

int main() {
    test_frame_post();
    test_split_delivery();
    test_health_get();
    test_refusals();
    test_percent_decode();
    test_parse_number();

    std::cout << "\n"
              << (failures == 0 ? "all checks passed"
                                : "some checks failed")
              << "\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
