//
// The length-framed TCP protocol between Apostle and Disciple.
//
// Section 2 of docs/PROTOCOL.md, and nothing beyond it. Every message
// in both directions carries the same twelve byte envelope, so a peer
// can always tell where the next one starts without parsing what came
// before. All integers are big-endian.
//
// There is no I/O here. Bytes go in, messages come out, which is what
// makes the framing testable without a socket.
//
// This file is duplicated in the Apostle and Disciple projects rather
// than shared. They are separate programs that ship separately, and
// docs/PROTOCOL.md is the source of truth both copies answer to; a
// change to one that is not made to the other is a protocol break
// either way.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef APOSTLE_WIRE_H
#define APOSTLE_WIRE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace apostle {

// 'T' 'C' 'S' 'T'. The first four bytes of every message.
inline constexpr std::uint32_t wire_magic = 0x54435354;

// Envelope revision. A peer that reads a different one closes the
// connection rather than guessing.
inline constexpr std::uint8_t wire_version = 1;

// Bytes before the payload: magic, version, type, flags, length.
inline constexpr std::size_t envelope_bytes = 12;

// Largest payload the envelope allows, from docs/PROTOCOL.md.
inline constexpr std::uint32_t max_payload_bytes = 16777216;

// Bytes of fixed header at the front of a frame payload.
inline constexpr std::size_t frame_header_bytes = 16;

//
// What a message is.
//
enum class message_type : std::uint8_t {
    hello = 0x01,
    welcome = 0x02,
    frame = 0x03,
    ack = 0x04,
    ping = 0x05,
    pong = 0x06,
    bye = 0x07
};

//
// How the image in a frame payload is encoded.
//
enum class frame_format : std::uint8_t {
    png = 1,
    jpeg = 2,
    raw_gray8 = 3,
    gray_packed = 4
};

//
// The fixed header at the front of a frame payload.
//
struct frame_header {
    std::uint32_t sequence = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    frame_format format = frame_format::png;

    // Shades for the raw formats, zero for png and jpeg.
    std::uint8_t gray_levels = 0;
};

//
// Name a message type for a log line.
//
const char *message_type_name(message_type type);

//
// Append one complete message to a buffer.
//
// Parameters:
//      type        - Message type.
//      payload     - Payload bytes, may be null when size is zero.
//      size        - Payload length.
//      out         - Buffer to append the envelope and payload to.
//
void encode_message(message_type type, const std::uint8_t *payload,
                    std::size_t size, std::vector<std::uint8_t> &out);

//
// Append a message whose payload is text, which every JSON message is.
//
void encode_message(message_type type, std::string_view payload,
                    std::vector<std::uint8_t> &out);

//
// How far decode_message() got.
//
enum class decode_result {
    // A whole message is not in the buffer yet.
    need_more,
    // One message is available.
    done,
    // The stream is not this protocol. The connection must be closed
    // without attempting to resynchronise.
    failed
};

//
// Look at the front of a buffer for one complete message.
//
// Parameters:
//      data, size      - Bytes received so far, from the start of a
//                        message.
//      type            - Receives the message type.
//      payload_offset  - Receives the offset of the payload within
//                        the buffer, always envelope_bytes.
//      payload_size    - Receives the payload length.
//      error           - Receives a reason when the result is failed.
//
// Returns:
//      Whether a message is available.
//
// Notes:
//      Nothing is consumed. On done, the caller removes
//      payload_offset + payload_size bytes from the front of its
//      buffer once it has finished with the payload.
//
decode_result decode_message(const std::uint8_t *data, std::size_t size,
                             message_type &type,
                             std::size_t &payload_offset,
                             std::size_t &payload_size,
                             std::string &error);

//
// Append a frame header to a buffer.
//
void encode_frame_header(const frame_header &header,
                         std::vector<std::uint8_t> &out);

//
// Read a frame header from the front of a frame payload.
//
// Parameters:
//      data, size  - The frame payload.
//      out         - Receives the header.
//
// Returns:
//      True when the payload was long enough and the header was
//      well formed. The image bytes then start at frame_header_bytes.
//
bool decode_frame_header(const std::uint8_t *data, std::size_t size,
                         frame_header &out);

//
// Find a string value in a flat JSON object.
//
// Parameters:
//      text        - The JSON text.
//      key         - Key to look for.
//      value       - Receives the decoded string.
//
// Returns:
//      True when the key was found with a string value.
//
// Notes:
//      This is not a JSON parser and does not pretend to be one. The
//      protocol's payloads are flat objects of strings, numbers, and
//      one array of strings, all generated by the programs in this
//      repository. It handles exactly that, and refuses rather than
//      guesses on anything else. If a payload ever needs nesting, it
//      needs a real parser first.
//
bool json_string(std::string_view text, std::string_view key,
                 std::string &value);

//
// Find a numeric value in a flat JSON object.
//
bool json_number(std::string_view text, std::string_view key,
                 long long &value);

//
// Whether a flat JSON array of strings holds a given value.
//
// Parameters:
//      text        - The JSON text.
//      key         - Key whose value is the array.
//      wanted      - String to look for among the elements.
//
bool json_array_has(std::string_view text, std::string_view key,
                    std::string_view wanted);

//
// Quote a string as a JSON value.
//
std::string json_quoted(std::string_view text);

} // namespace apostle

#endif // APOSTLE_WIRE_H
