//
// Minimal image header reader for Evangelist.
//
// Runs in the background context. Reports the pixel dimensions of a
// captured PNG or JPEG by parsing its header, so the daemon can be
// told the true frame size without decoding megabytes of pixels or
// trusting numbers reported by the page.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

// PNG files always start with this eight byte signature.
const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

//
// Read the pixel dimensions of an encoded image.
//
// Parameters:
//      bytes       - Uint8Array holding the whole encoded image.
//
// Returns:
//      Object with numeric `width` and `height`, or null when the
//      data is not a PNG or JPEG we can read.
//
// Notes:
//      Callers should treat null as "unknown" and carry on. A missing
//      dimension header is not worth failing a broadcast over.
//
export function imageSize(bytes) {
    if (isPng(bytes)) {
        return pngSize(bytes);
    }
    if (bytes.length > 2 && bytes[0] === 0xff && bytes[1] === 0xd8) {
        return jpegSize(bytes);
    }
    return null;
}

//
// Test the PNG signature.
//
function isPng(bytes) {
    if (bytes.length < PNG_SIGNATURE.length) {
        return false;
    }
    return PNG_SIGNATURE.every((b, i) => bytes[i] === b);
}

//
// Read width and height from a PNG IHDR chunk.
//
// Notes:
//      IHDR is required by the spec to be the first chunk, so its
//      payload always starts at byte 16.
//
function pngSize(bytes) {
    if (bytes.length < 24) {
        return null;
    }
    const view = new DataView(
        bytes.buffer,
        bytes.byteOffset,
        bytes.byteLength
    );
    return { width: view.getUint32(16), height: view.getUint32(20) };
}

//
// Read width and height from the first JPEG start-of-frame marker.
//
// Notes:
//      Walks the segment chain rather than guessing an offset,
//      because a capture may carry EXIF or ICC segments of any size
//      before the frame header.
//
function jpegSize(bytes) {
    const view = new DataView(
        bytes.buffer,
        bytes.byteOffset,
        bytes.byteLength
    );
    // Skip the two byte SOI marker.
    let offset = 2;

    while (offset + 3 < bytes.length) {
        if (bytes[offset] !== 0xff) {
            return null;
        }
        const marker = bytes[offset + 1];

        // Standalone markers carry no length field.
        if (marker === 0xd8 || marker === 0x01 ||
            (marker >= 0xd0 && marker <= 0xd7)) {
            offset += 2;
            continue;
        }
        // Start of scan means the header section is over.
        if (marker === 0xda) {
            return null;
        }

        const length = view.getUint16(offset + 2);
        if (length < 2) {
            return null;
        }

        // SOF0 through SOF15, excluding the DHT/JPG/DAC markers that
        // share the range, all begin with precision, height, width.
        const isFrameHeader =
            marker >= 0xc0 && marker <= 0xcf &&
            marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc;

        if (isFrameHeader) {
            if (offset + 9 > bytes.length) {
                return null;
            }
            return {
                height: view.getUint16(offset + 5),
                width: view.getUint16(offset + 7)
            };
        }

        offset += 2 + length;
    }
    return null;
}
