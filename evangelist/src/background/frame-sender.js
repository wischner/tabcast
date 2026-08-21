//
// Frame capture and upload for Evangelist.
//
// Runs in the background context, which is not an accident: content
// scripts are subject to the host page's CORS policy and cannot post
// to the loopback daemon, and tabs.captureVisibleTab() is not exposed
// to them at all.
//
// captureVisibleTab() needs the literal "<all_urls>" host permission,
// which is why the manifest asks for that rather than the http and
// https patterns the content scripts match on. Firefox gates the API
// on the exact string and does not accept the broader patterns as an
// equivalent; worse, it does not inject the method at all when the
// permission is missing, so the symptom is "captureVisibleTab is not
// a function" rather than anything mentioning permissions. Narrowing
// the manifest back to http/https breaks Firefox and leaves no clue
// why.
//
// One tick is: photograph the visible tab, hash it, drop it if it is
// identical to the previous frame, otherwise POST the raw image to
// Apostle. See docs/PROTOCOL.md for the wire format.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import { api } from "../common/shared.js";
import { frameUrl, healthUrl } from "../common/config.js";
import { imageSize } from "./image-info.js";

// Give up on a POST after this long. A frame travels over loopback,
// so anything slower than this means the daemon is wedged, and we
// would rather report that than pile up requests.
const REQUEST_TIMEOUT_MS = 8000;

// Page titles and URLs are attached as headers, which must stay
// short and ASCII. Anything longer than this is truncated.
const MAX_HEADER_TEXT = 512;

//
// Photograph the visible tab of a window.
//
// Parameters:
//      windowId    - Window whose active tab is captured.
//      settings    - Validated settings object, supplies format and
//                    JPEG quality.
//
// Returns:
//      Promise resolving to { bytes, mimeType, width, height }, where
//      `bytes` is a Uint8Array of the encoded image and width/height
//      may be null if the header could not be read.
//
// Notes:
//      Captures whatever tab is active in that window right now, not
//      necessarily the tab that asked. The caller must confirm the
//      requesting tab is the active one first.
//
export async function captureFrame(windowId, settings) {
    const options =
        settings.format === "jpeg"
            ? { format: "jpeg", quality: settings.jpegQuality }
            : { format: "png" };

    const dataUrl = await api.tabs.captureVisibleTab(windowId, options);
    const { bytes, mimeType } = decodeDataUrl(dataUrl);
    const size = imageSize(bytes) ?? { width: null, height: null };

    return { bytes, mimeType, width: size.width, height: size.height };
}

//
// Hash an encoded frame.
//
// Parameters:
//      bytes       - Uint8Array of the encoded image.
//
// Returns:
//      Promise resolving to a lowercase hex SHA-256 string.
//
// Notes:
//      Used only to notice that nothing on the page changed. E-ink
//      panels refresh slowly and visibly, so a repeated frame is
//      worse than no frame.
//
export async function hashFrame(bytes) {
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    return [...new Uint8Array(digest)]
        .map((b) => b.toString(16).padStart(2, "0"))
        .join("");
}

//
// POST one frame to the Apostle daemon.
//
// Parameters:
//      frame       - Object from captureFrame().
//      meta        - { sessionId, sequence, title, url } describing
//                    this frame's place in the broadcast.
//      settings    - Validated settings object.
//
// Returns:
//      Promise resolving to the daemon's parsed JSON reply, or to an
//      empty object when the daemon answers 200 without a body.
//
// Throws:
//      Error whose message is written for a human and is safe to
//      show in the control bar.
//
export async function postFrame(frame, meta, settings) {
    const headers = {
        "Content-Type": frame.mimeType,
        "X-Tabcast-Session": meta.sessionId,
        "X-Tabcast-Sequence": String(meta.sequence)
    };
    if (frame.width && frame.height) {
        headers["X-Tabcast-Width"] = String(frame.width);
        headers["X-Tabcast-Height"] = String(frame.height);
    }
    if (meta.title) {
        headers["X-Tabcast-Title"] = headerText(meta.title);
    }
    if (meta.url) {
        headers["X-Tabcast-Url"] = headerText(meta.url);
    }

    let response;
    try {
        response = await fetch(frameUrl(settings), {
            method: "POST",
            headers,
            body: frame.bytes,
            cache: "no-store",
            signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS)
        });
    } catch (cause) {
        throw new Error(unreachableMessage(settings, cause), { cause });
    }

    if (!response.ok) {
        throw new Error(await serverErrorMessage(response));
    }
    return (await readJson(response)) ?? {};
}

//
// Ask the daemon whether it is alive.
//
// Parameters:
//      settings    - Validated settings object.
//
// Returns:
//      Promise resolving to { ok: true, info } with the daemon's
//      health payload, or { ok: false, error } with a message meant
//      for the options page. Never rejects.
//
export async function probeDaemon(settings) {
    try {
        const response = await fetch(healthUrl(settings), {
            cache: "no-store",
            signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS)
        });
        if (!response.ok) {
            return { ok: false, error: await serverErrorMessage(response) };
        }
        return { ok: true, info: (await readJson(response)) ?? {} };
    } catch (cause) {
        return { ok: false, error: unreachableMessage(settings, cause) };
    }
}

//
// Split a "data:<mime>;base64,<payload>" URL into its parts.
//
// Parameters:
//      dataUrl     - Data URL as returned by captureVisibleTab().
//
// Returns:
//      { bytes, mimeType } with bytes as a Uint8Array.
//
// Notes:
//      Decoded by hand rather than with fetch(), because fetching a
//      data URL is not reliably available in a service worker and
//      this is a handful of lines.
//
function decodeDataUrl(dataUrl) {
    const comma = dataUrl.indexOf(",");
    if (!dataUrl.startsWith("data:") || comma < 0) {
        throw new Error("The browser returned an unreadable capture.");
    }
    const mimeType = dataUrl.slice(5, comma).split(";")[0] || "image/png";
    const binary = atob(dataUrl.slice(comma + 1));
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i += 1) {
        bytes[i] = binary.charCodeAt(i);
    }
    return { bytes, mimeType };
}

//
// Make arbitrary text safe to put in an HTTP header value.
//
// Parameters:
//      text        - Page title or URL, any Unicode.
//
// Returns:
//      Percent-encoded ASCII string, truncated to a sane length.
//
function headerText(text) {
    return encodeURIComponent(String(text).slice(0, MAX_HEADER_TEXT));
}

//
// Read a JSON body without letting a malformed one become the error
// the user sees.
//
// Returns:
//      Promise resolving to the parsed object, or null.
//
async function readJson(response) {
    try {
        return await response.json();
    } catch {
        return null;
    }
}

//
// Build the message shown when the daemon cannot be reached at all.
//
// Notes:
//      Kept short deliberately. This lands in the control bar's
//      status line, which sits on somebody else's page and has one
//      line to work with. A sentence that has to be truncated to
//      "Cannot reach Apos..." tells the reader less than a short one
//      that fits.
//
function unreachableMessage(settings, cause) {
    const where = `${settings.host}:${settings.port}`;
    if (cause?.name === "TimeoutError") {
        return `Apostle at ${where} did not answer in time.`;
    }
    return `No answer from Apostle at ${where}.`;
}

//
// Build the message shown when the daemon answers with an error.
//
// Notes:
//      Prefers the daemon's own `error` field, because it knows more
//      about why it said no than the status code does.
//
async function serverErrorMessage(response) {
    const body = await readJson(response);
    if (body?.error) {
        return `Apostle refused the frame: ${body.error}`;
    }
    return `Apostle answered ${response.status} ${response.statusText}.`;
}
