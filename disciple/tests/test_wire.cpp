//
// Tests for the length-framed TCP protocol.
//
// The envelope is what both programs have to agree on exactly, so it
// is checked byte by byte against docs/PROTOCOL.md rather than against
// whatever the encoder happens to produce. A test that only round
// trips would pass just as happily on a protocol neither peer speaks.
//
// This file is duplicated in the Apostle and Disciple projects,
// because the code it tests is. See the wire header.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/wire.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool passed, const std::string &what) {
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << "\n";
    if (!passed) {
        ++failures;
    }
}

using namespace disciple;

//
// The envelope laid out exactly as the protocol document describes it.
//
void test_envelope_bytes() {
    std::vector<std::uint8_t> out;
    const std::uint8_t payload[] = {0xde, 0xad};
    encode_message(message_type::frame, payload, sizeof(payload), out);

    check(out.size() == envelope_bytes + 2,
          "an envelope is twelve bytes plus the payload");
    check(out[0] == 'T' && out[1] == 'C' && out[2] == 'S' &&
              out[3] == 'T',
          "the magic is TCST");
    check(out[4] == 1, "the version is 1");
    check(out[5] == 0x03, "frame is type 0x03");
    check(out[6] == 0 && out[7] == 0, "the flags are reserved zero");
    check(out[8] == 0 && out[9] == 0 && out[10] == 0 && out[11] == 2,
          "the length is big-endian");
    check(out[12] == 0xde && out[13] == 0xad,
          "the payload follows the envelope");
}

//
// Decoding, including the part where bytes arrive in pieces.
//
void test_decode() {
    std::vector<std::uint8_t> out;
    encode_message(message_type::hello, "{\"protocol\":1}", out);

    message_type type = message_type::ping;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string error;

    check(decode_message(out.data(), 4, type, offset, size, error) ==
              decode_result::need_more,
          "four bytes is not a message");
    check(decode_message(out.data(), envelope_bytes + 2, type, offset,
                         size, error) == decode_result::need_more,
          "a whole envelope with half a payload is not a message");

    check(decode_message(out.data(), out.size(), type, offset, size,
                         error) == decode_result::done,
          "the complete message decodes");
    check(type == message_type::hello, "the type survives");
    check(offset == envelope_bytes, "the payload starts after twelve");
    check(size == 14, "the payload length survives");
}

//
// Everything that must close the connection rather than be tolerated.
//
void test_decode_refusals() {
    message_type type = message_type::ping;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string error;

    {
        std::vector<std::uint8_t> bad(envelope_bytes, 0);
        bad[0] = 'X';
        check(decode_message(bad.data(), bad.size(), type, offset, size,
                             error) == decode_result::failed,
              "bad magic fails");
    }
    {
        std::vector<std::uint8_t> out;
        encode_message(message_type::ping, nullptr, 0, out);
        out[4] = 2;
        check(decode_message(out.data(), out.size(), type, offset, size,
                             error) == decode_result::failed,
              "an unknown version fails");
    }
    {
        std::vector<std::uint8_t> out;
        encode_message(message_type::ping, nullptr, 0, out);
        out[5] = 0x42;
        check(decode_message(out.data(), out.size(), type, offset, size,
                             error) == decode_result::failed,
              "an unknown message type fails");
    }
    {
        std::vector<std::uint8_t> out;
        encode_message(message_type::ping, nullptr, 0, out);
        out[7] = 1;
        check(decode_message(out.data(), out.size(), type, offset, size,
                             error) == decode_result::failed,
              "a non-zero reserved flag fails rather than being "
              "ignored");
    }
    {
        std::vector<std::uint8_t> out;
        encode_message(message_type::frame, nullptr, 0, out);
        out[8] = 0xff; // a length far past the protocol limit
        check(decode_message(out.data(), out.size(), type, offset, size,
                             error) == decode_result::failed,
              "a payload over the limit fails before it is allocated");
    }
}

//
// The sixteen byte frame header.
//
void test_frame_header() {
    frame_header written;
    written.sequence = 70000;
    written.width = 1080;
    written.height = 1835;
    written.format = frame_format::png;
    written.gray_levels = 0;

    std::vector<std::uint8_t> out;
    encode_frame_header(written, out);
    check(out.size() == frame_header_bytes,
          "a frame header is sixteen bytes");

    frame_header read;
    check(decode_frame_header(out.data(), out.size(), read),
          "it decodes");
    check(read.sequence == 70000, "the sequence survives past 65535");
    check(read.width == 1080 && read.height == 1835,
          "the size survives");
    check(read.format == frame_format::png, "the format survives");

    check(!decode_frame_header(out.data(), 8, read),
          "a short header is refused");

    out[8] = 9;
    check(!decode_frame_header(out.data(), out.size(), read),
          "an unknown format is refused");
}

//
// The flat JSON payloads.
//
void test_json() {
    const std::string hello =
        "{\"name\":\"disciple\",\"protocol\":1,\"width\":480,"
        "\"height\":800,\"grayLevels\":4,"
        "\"accepts\":[\"image/png\",\"image/jpeg\"]}";

    std::string text;
    long long number = 0;

    check(json_string(hello, "name", text) && text == "disciple",
          "a string value is found");
    check(json_number(hello, "height", number) && number == 800,
          "a number value is found");
    check(json_array_has(hello, "accepts", "image/png"),
          "an array member is found");
    check(!json_array_has(hello, "accepts", "image/gray-packed"),
          "a member that is not there is not found");
    check(!json_string(hello, "missing", text),
          "an absent key is absent");
    check(!json_number(hello, "name", number),
          "a string is not read as a number");

    check(json_quoted("a\"b\\c") == "\"a\\\"b\\\\c\"",
          "quoting escapes quotes and backslashes");

    const std::string escaped = "{\"reason\":\"say \\\"no\\\"\"}";
    check(json_string(escaped, "reason", text) && text == "say \"no\"",
          "an escaped quote decodes");
}

} // namespace

int main() {
    test_envelope_bytes();
    test_decode();
    test_decode_refusals();
    test_frame_header();
    test_json();

    std::cout << "\n"
              << (failures == 0 ? "all checks passed"
                                : "some checks failed")
              << "\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
