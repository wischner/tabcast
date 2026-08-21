//
// Minimal HTTP/1.1 request parsing for the Apostle ingest endpoint.
//
// The whole file is byte handling with no I/O in it, which is the
// point: every decision about what counts as a valid request can be
// tested from a string literal.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/http.h"

#include <algorithm>
#include <cctype>

namespace apostle {
namespace {

//
// Compare two strings ignoring case, ASCII only.
//
bool equal_no_case(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

//
// Drop spaces and tabs from both ends.
//
std::string_view trim(std::string_view text) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

//
// Value of one hex digit, or -1.
//
int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

const std::string *http_request::header(std::string_view name) const {
    for (const auto &entry : headers) {
        if (equal_no_case(entry.first, name)) {
            return &entry.second;
        }
    }
    return nullptr;
}

request_parser::request_parser(std::size_t max_body_bytes)
    : _max_body_bytes(max_body_bytes) {
}

const http_request &request_parser::request() const {
    return _request;
}

const std::string &request_parser::error() const {
    return _error;
}

int request_parser::status() const {
    return _status;
}

void request_parser::reset() {
    _buffer.clear();
    _request = http_request();
    _error.clear();
    _status = 400;
    _body_offset = 0;
    _content_length = 0;
    _head_done = false;
    _state = parse_result::need_more;
}

parse_result request_parser::fail(int status, std::string reason) {
    _status = status;
    _error = std::move(reason);
    _state = parse_result::failed;
    return _state;
}

//
// Parse the request line and the header block, which are already
// known to be complete in _buffer up to _body_offset.
//
parse_result request_parser::parse_head() {
    const std::string_view head(
        reinterpret_cast<const char *>(_buffer.data()),
        _body_offset >= 4 ? _body_offset - 4 : 0);

    std::size_t line_start = 0;
    bool first_line = true;

    while (line_start <= head.size()) {
        std::size_t line_end = head.find("\r\n", line_start);
        if (line_end == std::string_view::npos) {
            line_end = head.size();
        }
        const std::string_view line =
            head.substr(line_start, line_end - line_start);

        if (first_line) {
            // METHOD SP TARGET SP HTTP/1.1
            const std::size_t first_space = line.find(' ');
            if (first_space == std::string_view::npos) {
                return fail(400, "malformed request line");
            }
            const std::size_t second_space =
                line.find(' ', first_space + 1);
            if (second_space == std::string_view::npos) {
                return fail(400, "malformed request line");
            }

            _request.method = std::string(line.substr(0, first_space));
            _request.target = std::string(line.substr(
                first_space + 1, second_space - first_space - 1));

            const std::string_view version =
                line.substr(second_space + 1);
            if (!version.starts_with("HTTP/1.")) {
                return fail(400, "unsupported HTTP version");
            }
            first_line = false;
        } else if (!line.empty()) {
            // A leading space would mean an obsolete folded header.
            // Nothing Evangelist sends needs one, and accepting them
            // is how header smuggling bugs start.
            if (line.front() == ' ' || line.front() == '\t') {
                return fail(400, "folded header lines are not accepted");
            }

            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                return fail(400, "malformed header line");
            }
            _request.headers.emplace_back(
                std::string(trim(line.substr(0, colon))),
                std::string(trim(line.substr(colon + 1))));
        }

        if (line_end >= head.size()) {
            break;
        }
        line_start = line_end + 2;
    }

    if (_request.method.empty()) {
        return fail(400, "empty request");
    }

    // Chunked bodies are not accepted. Evangelist always knows the
    // frame size before it posts, so a chunked upload here means
    // something other than Evangelist is talking to us.
    if (const std::string *encoding =
            _request.header("Transfer-Encoding")) {
        if (!encoding->empty()) {
            return fail(400, "Transfer-Encoding is not supported");
        }
    }

    _content_length = 0;
    if (const std::string *length = _request.header("Content-Length")) {
        long long parsed = 0;
        if (!parse_number(*length, parsed)) {
            return fail(400, "malformed Content-Length");
        }
        if (static_cast<unsigned long long>(parsed) > _max_body_bytes) {
            return fail(413, "frame too large");
        }
        _content_length = static_cast<std::size_t>(parsed);
    } else if (_request.method == "POST") {
        return fail(400, "missing Content-Length");
    }

    _head_done = true;
    return parse_result::need_more;
}

parse_result request_parser::feed(const std::uint8_t *data,
                                  std::size_t size) {
    if (_state != parse_result::need_more) {
        return _state;
    }

    _buffer.insert(_buffer.end(), data, data + size);

    if (!_head_done) {
        // Search for the blank line that ends the head. Restarting the
        // search from the beginning each time is fine at these sizes,
        // and the head is capped just below.
        static const std::uint8_t terminator[] = {'\r', '\n', '\r', '\n'};
        const auto found = std::search(_buffer.begin(), _buffer.end(),
                                       std::begin(terminator),
                                       std::end(terminator));
        if (found == _buffer.end()) {
            if (_buffer.size() > max_request_head_bytes) {
                return fail(431, "request headers too large");
            }
            return parse_result::need_more;
        }

        _body_offset =
            static_cast<std::size_t>(found - _buffer.begin()) + 4;
        if (_body_offset > max_request_head_bytes) {
            return fail(431, "request headers too large");
        }

        const parse_result head = parse_head();
        if (head == parse_result::failed) {
            return head;
        }
    }

    if (_buffer.size() - _body_offset < _content_length) {
        return parse_result::need_more;
    }

    _request.body.assign(_buffer.begin() + _body_offset,
                         _buffer.begin() + _body_offset + _content_length);
    _state = parse_result::done;
    return _state;
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int high = hex_value(text[i + 1]);
            const int low = hex_value(text[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>(high * 16 + low));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

bool parse_number(std::string_view text, long long &value) {
    const std::string_view digits = trim(text);
    if (digits.empty() || digits.size() > 18) {
        return false;
    }

    long long result = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + (c - '0');
    }
    value = result;
    return true;
}

} // namespace apostle
