//
// Minimal HTTP/1.1 request parsing for the Apostle ingest endpoint.
//
// Deliberately not a general purpose HTTP library. It understands
// exactly what Evangelist sends over loopback: a request line, a
// handful of headers, and an optional body whose length is given by
// Content-Length. Chunked transfer, pipelining, upgrades, and
// continuation lines are all rejected rather than half-supported,
// because a half-supported feature on an ingest path is a source of
// silent corruption.
//
// Kept apart from the socket code so it can be tested without a
// network. See tests/test_http.cpp.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_HTTP_H
#define APOSTLE_HTTP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apostle {

// Largest request line plus header block accepted. A frame's bytes are
// the body and are not counted here, so this only has to be big enough
// for a page title and a URL.
inline constexpr std::size_t max_request_head_bytes = 16 * 1024;

//
// One parsed request.
//
struct http_request {
    std::string method;
    std::string target;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::uint8_t> body;

    //
    // Look up a header by name.
    //
    // Parameters:
    //      name        - Header name, matched without regard to case.
    //
    // Returns:
    //      Pointer to the value, or null when the header is absent.
    //      The pointer is valid until the request is modified.
    //
    const std::string *header(std::string_view name) const;
};

//
// How far feed() got.
//
enum class parse_result {
    // Nothing is wrong, there is simply not enough input yet.
    need_more,
    // A complete request is available from request().
    done,
    // The input cannot be a request this daemon will serve. status()
    // says what to answer and error() says why.
    failed
};

//
// An incremental request reader.
//
// Bytes arrive from a socket in whatever sizes the kernel hands over,
// so the parser is fed repeatedly and says each time whether it has a
// whole request yet.
//
class request_parser {
public:
    //
    // Construct a parser with a body size limit.
    //
    // Parameters:
    //      max_body_bytes - Largest Content-Length accepted. A larger
    //                       one is refused with 413 before a single
    //                       body byte is read, so an oversized upload
    //                       cannot exhaust memory.
    //
    explicit request_parser(std::size_t max_body_bytes);

    //
    // Add bytes read from the socket.
    //
    // Parameters:
    //      data, size  - Bytes just read.
    //
    // Returns:
    //      Whether a complete request is now available.
    //
    // Notes:
    //      Once this returns done or failed it keeps returning the
    //      same answer until reset() is called.
    //
    parse_result feed(const std::uint8_t *data, std::size_t size);

    //
    // The parsed request. Only meaningful after feed() returned done.
    //
    const http_request &request() const;

    //
    // Why parsing failed, in a form fit for a log line.
    //
    const std::string &error() const;

    //
    // The HTTP status that describes the failure, 400 unless the
    // request was well formed but unacceptable.
    //
    int status() const;

    //
    // Forget everything and start over on a fresh request.
    //
    void reset();

private:
    parse_result fail(int status, std::string reason);
    parse_result parse_head();

    std::size_t _max_body_bytes;
    std::vector<std::uint8_t> _buffer;
    http_request _request;
    std::string _error;
    int _status = 400;
    std::size_t _body_offset = 0;
    std::size_t _content_length = 0;
    bool _head_done = false;
    parse_result _state = parse_result::need_more;
};

//
// Decode percent escapes in a header value.
//
// Parameters:
//      text        - Percent-encoded text, as sent in X-Tabcast-Title
//                    and X-Tabcast-Url.
//
// Returns:
//      The decoded string. An escape that is not two hex digits is
//      left alone rather than dropped, because a mangled title is
//      better than a lost one and this value is only ever logged.
//
std::string percent_decode(std::string_view text);

//
// Parse a header value as a non-negative integer.
//
// Parameters:
//      text        - Header value, surrounding spaces already
//                    trimmed by the parser.
//      value       - Receives the parsed number.
//
// Returns:
//      True when the whole value was digits and fit.
//
bool parse_number(std::string_view text, long long &value);

} // namespace apostle

#endif // APOSTLE_HTTP_H
