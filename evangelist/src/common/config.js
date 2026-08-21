//
// Settings storage for Evangelist.
//
// Runs in the background context and on the options page. Owns the
// single `settings` object in storage.local: reading it with defaults
// filled in, clamping anything out of range, and writing it back.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import { api, DEFAULTS, LIMITS, SETTINGS_KEY } from "./shared.js";

//
// Clamp a number into an inclusive range, falling back to a default
// when the value is not a finite number at all.
//
// Parameters:
//      value       - Candidate value, from storage or from a form.
//      range       - Object with `min` and `max`.
//      fallback    - Value to use when `value` is not a number.
//
// Returns:
//      A number inside [range.min, range.max].
//
function clamp(value, range, fallback) {
    const n = Number(value);
    if (!Number.isFinite(n)) {
        return fallback;
    }
    return Math.min(range.max, Math.max(range.min, Math.round(n)));
}

//
// Normalise a raw settings object into a complete, valid one.
//
// Parameters:
//      raw         - Partial or untrusted settings object, may be
//                    null.
//
// Returns:
//      A new settings object with every key present and in range.
//
// Notes:
//      Exported because the options page validates its form with it
//      before saving.
//
export function normaliseSettings(raw) {
    const s = { ...DEFAULTS, ...(raw ?? {}) };
    return {
        intervalSeconds: clamp(
            s.intervalSeconds,
            LIMITS.intervalSeconds,
            DEFAULTS.intervalSeconds
        ),
        host: String(s.host || DEFAULTS.host).trim(),
        port: clamp(s.port, LIMITS.port, DEFAULTS.port),
        format: LIMITS.formats.includes(s.format)
            ? s.format
            : DEFAULTS.format,
        jpegQuality: clamp(
            s.jpegQuality,
            LIMITS.jpegQuality,
            DEFAULTS.jpegQuality
        ),
        skipUnchanged: Boolean(s.skipUnchanged),
        showControlBar: Boolean(s.showControlBar)
    };
}

//
// Read the current settings.
//
// Returns:
//      Promise resolving to a complete, validated settings object.
//      Never rejects on a missing or corrupt stored value, it falls
//      back to defaults instead.
//
export async function loadSettings() {
    try {
        const stored = await api.storage.local.get(SETTINGS_KEY);
        return normaliseSettings(stored?.[SETTINGS_KEY]);
    } catch {
        return { ...DEFAULTS };
    }
}

//
// Merge a patch into the stored settings and persist the result.
//
// Parameters:
//      patch       - Partial settings object. Keys absent from the
//                    patch keep their current value.
//
// Returns:
//      Promise resolving to the settings object as it was written.
//
// Notes:
//      Writing fires storage.onChanged, which is how the control bar
//      in every open tab learns about the change.
//
export async function saveSettings(patch) {
    const merged = normaliseSettings({
        ...(await loadSettings()),
        ...patch
    });
    await api.storage.local.set({ [SETTINGS_KEY]: merged });
    return merged;
}

//
// Build the Apostle frame endpoint URL from settings.
//
// Parameters:
//      settings    - Validated settings object.
//
// Returns:
//      Absolute URL string, for example
//      "http://127.0.0.1:8420/api/v1/frame".
//
export function frameUrl(settings) {
    return `http://${settings.host}:${settings.port}/api/v1/frame`;
}

//
// Build the Apostle health endpoint URL from settings.
//
// Parameters:
//      settings    - Validated settings object.
//
// Returns:
//      Absolute URL string.
//
export function healthUrl(settings) {
    return `http://${settings.host}:${settings.port}/api/v1/health`;
}
