//
// Evangelist background entry point.
//
// Runs as a Chrome service worker and as a Firefox event page. It owns
// the broadcast session, answers every message from the control bar,
// and performs the capture and upload that a content script is not
// allowed to do.
//
// This context can be torn down at any moment between events. It
// therefore keeps no state in module variables, only in session.js.
// The capture clock also lives elsewhere, in the content script, for
// the same reason.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import { api, MSG } from "../common/shared.js";
import { loadSettings } from "../common/config.js";
import {
    getState,
    patchState,
    startSession,
    stopSession,
    viewFor
} from "./session.js";
import { captureFrame, hashFrame, postFrame, probeDaemon }
    from "./frame-sender.js";

// Toolbar badge shown while a broadcast is running.
const BADGE_TEXT = "ON";
const BADGE_COLOR = "#b91c1c";

//
// Route one runtime message.
//
// Parameters:
//      message     - Message object with a `type` from MSG.
//      sender      - Sender given by the browser. For a content
//                    script this carries the originating tab.
//
// Returns:
//      Promise resolving to the reply for that message type.
//
// Throws:
//      Error for an unknown message type or a message that arrives
//      without the tab context it needs.
//
async function handleMessage(message, sender) {
    const tabId = sender?.tab?.id ?? null;

    switch (message?.type) {
        case MSG.GET_STATE:
            return viewFor(await getState(), tabId);
        case MSG.START:
            return onStart(requireTab(sender));
        case MSG.STOP:
            return onStop(tabId);
        case MSG.TICK:
            return onTick(message, requireTab(sender));
        case MSG.PROBE:
            return probeDaemon(await loadSettings());
        default:
            throw new Error(`Unknown message type: ${message?.type}`);
    }
}

//
// Begin broadcasting the sender's tab.
//
// Returns:
//      Promise resolving to the control bar view for that tab.
//
// Notes:
//      Replaces any broadcast already running in another tab. The
//      displaced tab is told, so its bar falls back to idle rather
//      than showing a stop button that does nothing.
//
async function onStart(tab) {
    const previous = await getState();
    const state = await startSession(tab.id, tab.windowId);

    await setBadge(true);
    await announce(state, [previous.tabId, tab.id]);
    return viewFor(state, tab.id);
}

//
// Stop the current broadcast.
//
// Parameters:
//      tabId       - Tab that asked, or null when the request came
//                    from somewhere without a tab.
//
// Returns:
//      Promise resolving to the control bar view for that tab.
//
async function onStop(tabId) {
    const previous = await getState();
    const state = await stopSession();

    await setBadge(false);
    await announce(state, [previous.tabId, tabId]);
    return viewFor(state, tabId);
}

//
// Capture and send one frame on behalf of the broadcasting tab.
//
// Parameters:
//      message     - Tick message, carries the page `title` and
//                    `url` the content script observed.
//      tab         - Sending tab.
//
// Returns:
//      Promise resolving to { ok, skipped?, error?, view }. Rejection
//      is reserved for programming errors. A daemon that is down is a
//      normal, reportable outcome, not an exception.
//
async function onTick(message, tab) {
    let state = await getState();

    // The bar in a stale tab may keep ticking after a stop or after
    // another tab took over. Tell it to stand down.
    if (!state.broadcasting || state.tabId !== tab.id) {
        return { ok: false, stopped: true, view: viewFor(state, tab.id) };
    }

    // captureVisibleTab() photographs whichever tab is active in the
    // window, so a tick from a background tab would capture the wrong
    // page. The content script already skips hidden tabs, this is the
    // check that makes it safe rather than merely likely.
    if (!(await isActiveTab(tab.id))) {
        state = await patchState({ paused: true });
        return {
            ok: true,
            skipped: "hidden",
            view: viewFor(state, tab.id)
        };
    }

    const settings = await loadSettings();

    try {
        const frame = await captureFrame(state.windowId, settings);
        const hash = await hashFrame(frame.bytes);

        if (settings.skipUnchanged && hash === state.lastHash) {
            state = await patchState({ paused: false, lastError: null });
            return {
                ok: true,
                skipped: "unchanged",
                view: viewFor(state, tab.id)
            };
        }

        const sequence = state.sequence + 1;
        const reply = await postFrame(
            frame,
            {
                sessionId: state.sessionId,
                sequence,
                title: message.title,
                url: message.url
            },
            settings
        );

        state = await patchState({
            sequence,
            lastHash: hash,
            lastSentAt: Date.now(),
            lastError: null,
            clients: Number.isFinite(reply.clients) ? reply.clients : null,
            paused: false
        });
        return { ok: true, sent: true, view: viewFor(state, tab.id) };
    } catch (error) {
        state = await patchState({
            paused: false,
            lastError: error.message
        });
        return {
            ok: false,
            error: error.message,
            view: viewFor(state, tab.id)
        };
    }
}

//
// Test whether a tab is the active one in its window.
//
// Returns:
//      Promise resolving to true or false. A tab that has gone away
//      counts as not active.
//
async function isActiveTab(tabId) {
    try {
        const tab = await api.tabs.get(tabId);
        return Boolean(tab?.active);
    } catch {
        return false;
    }
}

//
// Push the new state to control bars.
//
// Parameters:
//      state       - Full session state.
//      tabIds      - Tabs that must hear about it, nulls and repeats
//                    allowed.
//
// Notes:
//      Every open tab is told, not just the interesting ones, so a
//      bar can display "another tab is broadcasting" instead of an
//      idle start button that would silently steal the session.
//
async function announce(state, tabIds = []) {
    const targets = new Set(tabIds.filter((id) => id !== null));

    try {
        for (const tab of await api.tabs.query({})) {
            targets.add(tab.id);
        }
    } catch {
        // Without the tab list we still reach the tabs we know about.
    }

    await Promise.all(
        [...targets].map((tabId) =>
            api.tabs
                .sendMessage(tabId, {
                    type: MSG.STATE_CHANGED,
                    view: viewFor(state, tabId)
                })
                // Most tabs have no control bar listening: an
                // extension page, a PDF, a tab that never finished
                // loading. Not an error.
                .catch(() => undefined)
        )
    );
}

//
// Show or clear the toolbar badge.
//
async function setBadge(on) {
    try {
        await api.action.setBadgeText({ text: on ? BADGE_TEXT : "" });
        if (on) {
            await api.action.setBadgeBackgroundColor({ color: BADGE_COLOR });
        }
    } catch {
        // A missing badge is cosmetic. Never let it break a broadcast.
    }
}

//
// Reject a message that needs a tab but did not come from one.
//
// Returns:
//      The sender's tab object.
//
// Throws:
//      Error when the message came from a context without a tab, such
//      as the options page.
//
function requireTab(sender) {
    if (!sender?.tab) {
        throw new Error("This action must come from a page.");
    }
    return sender.tab;
}

// Chrome does not accept a promise returned from an onMessage
// listener, and Firefox does not accept both styles at once. Answering
// through sendResponse and returning true works in both.
api.runtime.onMessage.addListener((message, sender, sendResponse) => {
    handleMessage(message, sender)
        .then(sendResponse)
        .catch((error) =>
            sendResponse({ ok: false, error: String(error?.message ?? error) })
        );
    return true;
});

// The toolbar button shows and hides the on-page control bar.
api.action.onClicked.addListener((tab) => {
    if (tab?.id === undefined) {
        return;
    }
    api.tabs
        .sendMessage(tab.id, { type: MSG.TOGGLE_UI })
        .catch(() =>
            // No content script here, so there is no bar to toggle.
            // Offer the settings instead, which is the only other
            // thing this button could usefully do.
            api.runtime.openOptionsPage?.()
        );
});

// A closed tab ends its broadcast. Nothing else can capture it.
api.tabs.onRemoved.addListener(async (tabId) => {
    const state = await getState();
    if (state.broadcasting && state.tabId === tabId) {
        await stopSession();
        await setBadge(false);
        await announce(await getState());
    }
});

// Start from a clean, known state whenever the extension is loaded or
// updated, so a stale session cannot outlive the code that made it.
api.runtime.onInstalled.addListener(async () => {
    await stopSession();
    await setBadge(false);
});
