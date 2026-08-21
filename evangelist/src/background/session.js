//
// Broadcast session state for Evangelist.
//
// Runs in the background context. Holds the answer to "is a broadcast
// running, and for which tab". The background can be torn down between
// two events, so the state is persisted rather than kept in a module
// variable. storage.session is used when the browser has it, which
// keeps the state out of the profile on disk, with storage.local as
// the fallback.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import { api } from "../common/shared.js";

const SESSION_KEY = "session";

// storage.session exists in Chrome 102+ and Firefox 115+. Everything
// older gets storage.local, which behaves identically for our purpose
// and only differs in that it survives a browser restart.
const backend = api.storage.session ?? api.storage.local;

// A broadcast that is not running. Also the shape of the whole state
// object, so this doubles as documentation.
const IDLE = Object.freeze({
    // True between a start and a stop.
    broadcasting: false,
    // Tab and window being captured, null when idle.
    tabId: null,
    windowId: null,
    // Opaque id identifying one continuous broadcast, sent to Apostle
    // so it can tell a reconnect from a new session.
    sessionId: null,
    // Frames accepted by Apostle so far in this session.
    sequence: 0,
    // SHA-256 of the last frame sent, used to drop repeats.
    lastHash: null,
    // Epoch milliseconds of the last accepted frame, null if none.
    lastSentAt: null,
    // Human readable reason the last attempt failed, null if fine.
    lastError: null,
    // Readers Apostle says it fanned the last frame out to.
    clients: null,
    // True while the tab is hidden and capture is suspended.
    paused: false
});

//
// Read the current broadcast state.
//
// Returns:
//      Promise resolving to a complete state object. Returns the idle
//      state when nothing is stored or storage is unreadable.
//
export async function getState() {
    try {
        const stored = await backend.get(SESSION_KEY);
        return { ...IDLE, ...(stored?.[SESSION_KEY] ?? {}) };
    } catch {
        return { ...IDLE };
    }
}

//
// Merge a patch into the broadcast state and persist it.
//
// Parameters:
//      patch       - Partial state object.
//
// Returns:
//      Promise resolving to the full state as it was written.
//
export async function patchState(patch) {
    const next = { ...(await getState()), ...patch };
    await backend.set({ [SESSION_KEY]: next });
    return next;
}

//
// Begin a broadcast session for one tab.
//
// Parameters:
//      tabId       - Tab to capture.
//      windowId    - Window that tab lives in, needed by
//                    tabs.captureVisibleTab().
//
// Returns:
//      Promise resolving to the new state.
//
// Notes:
//      Only one tab may broadcast at a time. Starting here silently
//      replaces any previous session, because captureVisibleTab() can
//      only ever photograph the tab the user is looking at.
//
export async function startSession(tabId, windowId) {
    return patchState({
        ...IDLE,
        broadcasting: true,
        tabId,
        windowId,
        sessionId: newSessionId()
    });
}

//
// End the current broadcast session.
//
// Returns:
//      Promise resolving to the idle state.
//
export async function stopSession() {
    return patchState({ ...IDLE });
}

//
// Reduce the full state to what a control bar needs to render.
//
// Parameters:
//      state       - Full state object.
//      tabId       - Tab asking for the view, may be null.
//
// Returns:
//      A small plain object, safe to send to a content script.
//
// Notes:
//      `owned` is the field the control bar cares about most: a tab
//      that is not the broadcasting tab must show "another tab is
//      broadcasting" rather than an active stop button.
//
export function viewFor(state, tabId) {
    return {
        broadcasting: state.broadcasting,
        owned: state.broadcasting && state.tabId === tabId,
        paused: state.paused,
        sequence: state.sequence,
        lastSentAt: state.lastSentAt,
        lastError: state.lastError,
        clients: state.clients
    };
}

//
// Generate an opaque session id.
//
// Returns:
//      Short lowercase hex string, unique enough to distinguish two
//      broadcasts on one machine.
//
function newSessionId() {
    const bytes = crypto.getRandomValues(new Uint8Array(8));
    return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}
