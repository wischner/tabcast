//
// The length-framed TCP protocol between Apostle and Disciple.
//
// Duplicated in both projects. See the header for why.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/wire.h"

#include <cctype>

namespace apostle {
namespace {

//
// Append a big-endian integer of a given width.
//
void put_big_endian(std::vector<std::uint8_t> &out,
                    unsigned long long value, int bytes) {
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

//
// Read a big-endian integer of a given width.
//
unsigned long long get_big_endian(const std::uint8_t *data, int bytes) {
    unsigned long long value = 0;
    for (int i = 0; i < bytes; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

//
// Whether a byte is JSON whitespace.
//
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//
// Find the position just past the colon of "key" : in a flat object.
//
// Returns:
//      Offset of the first value character, or npos.
//
// Notes:
//      Only looks at the top level, which for these payloads is the
//      only level. A key appearing inside a nested object would be
//      found too, which is exactly why the header says this must not
//      be pointed at nested JSON.
//
std::size_t find_value(std::string_view text, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t at = text.find(needle);
    if (at == std::string_view::npos) {
        return std::string_view::npos;
    }

    std::size_t i = at + needle.size();
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    if (i >= text.size() || text[i] != ':') {
        return std::string_view::npos;
    }
    ++i;
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    return i < text.size() ? i : std::string_view::npos;
}

} // namespace

const char *message_type_name(message_type type) {
    switch (type) {
    case message_type::hello:
        return "hello";
    case message_type::welcome:
        return "welcome";
    case message_type::frame:
        return "frame";
    case message_type::ack:
        return "ack";
    case message_type::ping:
        return "ping";
    case message_type::pong:
        return "pong";
    case message_type::bye:
        return "bye";
    default:
        return "unknown";
    }
}

void encode_message(message_type type, const std::uint8_t *payload,
                    std::size_t size, std::vector<std::uint8_t> &out) {
    out.reserve(out.size() + envelope_bytes + size);

    put_big_endian(out, wire_magic, 4);
    out.push_back(wire_version);
    out.push_back(static_cast<std::uint8_t>(type));
    put_big_endian(out, 0, 2); // flags, reserved
    put_big_endian(out, size, 4);

    if (payload != nullptr && size > 0) {
        out.insert(out.end(), payload, payload + size);
    }
}

void encode_message(message_type type, std::string_view payload,
                    std::vector<std::uint8_t> &out) {
    encode_message(type,
                   reinterpret_cast<const std::uint8_t *>(payload.data()),
                   payload.size(), out);
}

decode_result decode_message(const std::uint8_t *data, std::size_t size,
                             message_type &type,
                             std::size_t &payload_offset,
                             std::size_t &payload_size,
                             std::string &error) {
    if (size < envelope_bytes) {
        return decode_result::need_more;
    }

    if (get_big_endian(data, 4) != wire_magic) {
        error = "bad magic, this is not the Tabcast protocol";
        return decode_result::failed;
    }
    if (data[4] != wire_version) {
        error = "unsupported protocol version " +
                std::to_string(static_cast<int>(data[4]));
        return decode_result::failed;
    }

    const std::uint8_t raw_type = data[5];
    if (raw_type < static_cast<std::uint8_t>(message_type::hello) ||
        raw_type > static_cast<std::uint8_t>(message_type::bye)) {
        error = "unknown message type " +
                std::to_string(static_cast<int>(raw_type));
        return decode_result::failed;
    }

    // Flags are reserved and must be zero. Ignoring them would make
    // the first real use of a flag a silent misinterpretation.
    if (get_big_endian(data + 6, 2) != 0) {
        error = "reserved flags are not zero";
        return decode_result::failed;
    }

    const unsigned long long length = get_big_endian(data + 8, 4);
    if (length > max_payload_bytes) {
        error = "payload of " + std::to_string(length) +
                " bytes is over the protocol limit";
        return decode_result::failed;
    }

    if (size < envelope_bytes + length) {
        return decode_result::need_more;
    }

    type = static_cast<message_type>(raw_type);
    payload_offset = envelope_bytes;
    payload_size = static_cast<std::size_t>(length);
    return decode_result::done;
}

void encode_frame_header(const frame_header &header,
                         std::vector<std::uint8_t> &out) {
    put_big_endian(out, header.sequence, 4);
    put_big_endian(out, header.width, 2);
    put_big_endian(out, header.height, 2);
    out.push_back(static_cast<std::uint8_t>(header.format));
    out.push_back(header.gray_levels);
    put_big_endian(out, 0, 6); // reserved
}

bool decode_frame_header(const std::uint8_t *data, std::size_t size,
                         frame_header &out) {
    if (size < frame_header_bytes) {
        return false;
    }

    const std::uint8_t format = data[8];
    if (format < static_cast<std::uint8_t>(frame_format::png) ||
        format > static_cast<std::uint8_t>(frame_format::gray_packed)) {
        return false;
    }

    out.sequence = static_cast<std::uint32_t>(get_big_endian(data, 4));
    out.width = static_cast<std::uint16_t>(get_big_endian(data + 4, 2));
    out.height = static_cast<std::uint16_t>(get_big_endian(data + 6, 2));
    out.format = static_cast<frame_format>(format);
    out.gray_levels = data[9];
    return true;
}

bool json_string(std::string_view text, std::string_view key,
                 std::string &value) {
    std::size_t i = find_value(text, key);
    if (i == std::string_view::npos || text[i] != '"') {
        return false;
    }

    std::string out;
    for (++i; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            value = out;
            return true;
        }
        if (c == '\\' && i + 1 < text.size()) {
            ++i;
            switch (text[i]) {
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'r':
                out.push_back('\r');
                break;
            default:
                // Covers \" and \\, and leaves \uXXXX alone rather
                // than decoding half of it. No payload here uses one.
                out.push_back(text[i]);
                break;
            }
            continue;
        }
        out.push_back(c);
    }
    return false;
}

bool json_number(std::string_view text, std::string_view key,
                 long long &value) {
    std::size_t i = find_value(text, key);
    if (i == std::string_view::npos) {
        return false;
    }

    bool negative = false;
    if (text[i] == '-') {
        negative = true;
        ++i;
    }

    const std::size_t first_digit = i;
    long long result = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        result = result * 10 + (text[i] - '0');
        if (result > 1000000000000LL) {
            return false;
        }
    }
    if (i == first_digit) {
        return false;
    }

    value = negative ? -result : result;
    return true;
}

bool json_array_has(std::string_view text, std::string_view key,
                    std::string_view wanted) {
    std::size_t i = find_value(text, key);
    if (i == std::string_view::npos || text[i] != '[') {
        return false;
    }

    const std::size_t end = text.find(']', i);
    if (end == std::string_view::npos) {
        return false;
    }

    const std::string needle = "\"" + std::string(wanted) + "\"";
    return text.substr(i, end - i).find(needle) != std::string_view::npos;
}

std::string json_quoted(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');

    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char digits[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(c >> 4) & 0x0f]);
                out.push_back(digits[c & 0x0f]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }

    out.push_back('"');
    return out;
}

} // namespace apostle
