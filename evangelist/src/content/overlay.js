//
// Evangelist capture clock.
//
// Runs as a content script on every http and https page, alongside
// bar-ui.js, which owns the DOM. This file owns the behaviour: the
// broadcast state as the background reports it, the timer that decides
// when the next frame is due, and the messaging in between.
//
// The clock lives here rather than in the background on purpose. A
// Manifest V3 background context is torn down when idle and its timers
// die with it, whereas a page keeps its timers for as long as it is
// worth capturing. A hidden tab throttles this timer, which is exactly
// right, because a hidden tab cannot be photographed anyway.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

(function () {
    const { api, MSG, DEFAULTS, LIMITS, SETTINGS_KEY, ROOT_ID } = globalThis.TC;

    // Refuse to run twice in one document, for instance after a manual
    // re-injection during development.
    if (document.getElementById(ROOT_ID)) {
        return;
    }

    // Current settings, kept in step with storage by onChanged.
    let settings = { ...DEFAULTS };

    // Latest view of the broadcast as reported by the background.
    // `owned` means this tab is the one being captured.
    let view = {
        broadcasting: false,
        owned: false,
        paused: false,
        sequence: 0,
        lastError: null,
        clients: null
    };

    // Pending capture, and the wall clock time it is due, which is
    // what the "next in Ns" readout counts down to.
    let tickTimer = null;
    let tickDueAt = 0;

    // Once-a-second repaint, running only while a broadcast is live.
    let countdownTimer = null;

    // True while a start or stop request is in flight.
    let busy = false;

    // Per-page override of the bar's visibility. null means follow
    // the stored preference, true and false mean the user forced it
    // one way on this page with the close button or the toolbar
    // button. Kept out of storage on purpose: dismissing the bar on
    // one page should not silently reconfigure every other tab.
    let hideOverride = null;

    const ui = globalThis.TC_UI.build({
        onStart: () => guard(startBroadcast()),
        onStop: () => guard(stopBroadcast()),
        onIntervalInput: (seconds) => globalThis.TC_UI.setSeconds(ui, seconds),
        onIntervalCommit: (seconds) => guard(commitInterval(seconds)),
        onHide: () => {
            hideOverride = true;
            render();
        }
    });

    guard(startUp());

    //
    // Bring the bar up: load settings, learn the current broadcast
    // state, attach listeners, and resume capturing if this tab was
    // already broadcasting before it navigated.
    //
    async function startUp() {
        settings = normalise(
            (await api.storage.local.get(SETTINGS_KEY))?.[SETTINGS_KEY]
        );
        render();

        api.storage.onChanged.addListener(onStorageChanged);
        api.runtime.onMessage.addListener(onRuntimeMessage);
        document.addEventListener("visibilitychange", onVisibilityChange);

        setView(await send({ type: MSG.GET_STATE }));
    }

    // ---------------------------------------------------------------
    // Capture clock
    // ---------------------------------------------------------------

    //
    // Schedule the next capture, replacing any pending one.
    //
    // Parameters:
    //      delayMs     - Milliseconds to wait, defaults to the
    //                    configured interval.
    //
    function scheduleTick(delayMs = settings.intervalSeconds * 1000) {
        clearTick();
        tickDueAt = Date.now() + delayMs;
        tickTimer = setTimeout(() => guard(tick()), delayMs);
    }

    //
    // Cancel any pending capture.
    //
    function clearTick() {
        if (tickTimer !== null) {
            clearTimeout(tickTimer);
            tickTimer = null;
        }
        tickDueAt = 0;
    }

    //
    // Run one capture cycle, then schedule the next.
    //
    // Notes:
    //      A hidden tab is skipped without troubling the background,
    //      because tabs.captureVisibleTab() would photograph whatever
    //      tab the user switched to instead of this one.
    //
    async function tick() {
        if (!view.owned) {
            clearTick();
            return;
        }
        if (document.visibilityState !== "visible") {
            setView({ ...view, paused: true });
            scheduleTick();
            return;
        }

        const reply = await send({
            type: MSG.TICK,
            title: document.title,
            url: location.href
        });

        if (reply?.view) {
            setView(reply.view);
        }
        if (reply?.stopped) {
            clearTick();
            return;
        }
        scheduleTick();
    }

    // ---------------------------------------------------------------
    // Commands
    // ---------------------------------------------------------------

    //
    // Start broadcasting this tab.
    //
    // Notes:
    //      The first frame is not captured here. Taking ownership
    //      makes setView() schedule a tick due immediately, so
    //      capturing here as well would send the very first frame
    //      twice. Waiting a whole interval to discover the daemon is
    //      down would make the button feel broken, and that is what
    //      the zero delay in setView() is for.
    //
    async function startBroadcast() {
        await withBusy(async () => {
            setView(await send({ type: MSG.START }));
        });
    }

    //
    // Stop broadcasting.
    //
    async function stopBroadcast() {
        await withBusy(async () => {
            clearTick();
            setView(await send({ type: MSG.STOP }));
        });
    }

    //
    // Persist a new interval chosen on the slider.
    //
    // Parameters:
    //      seconds     - Raw slider value.
    //
    // Notes:
    //      Writing to storage fires onChanged, which is how the bar in
    //      every other tab and the options page pick the change up.
    //      A running broadcast is re-timed immediately rather than
    //      being left to finish a wait that may be minutes long.
    //
    async function commitInterval(seconds) {
        const intervalSeconds = clamp(
            seconds,
            LIMITS.intervalSeconds,
            DEFAULTS.intervalSeconds
        );
        settings = { ...settings, intervalSeconds };
        render();

        const stored = (await api.storage.local.get(SETTINGS_KEY))?.[
            SETTINGS_KEY
        ];
        await api.storage.local.set({
            [SETTINGS_KEY]: { ...(stored ?? {}), intervalSeconds }
        });

        if (view.owned) {
            scheduleTick();
        }
    }

    // ---------------------------------------------------------------
    // Events
    // ---------------------------------------------------------------

    //
    // Follow settings changed anywhere else: the options page, or the
    // bar in another tab.
    //
    function onStorageChanged(changes, area) {
        if (area !== "local" || !changes[SETTINGS_KEY]) {
            return;
        }
        const previous = settings;
        settings = normalise(changes[SETTINGS_KEY].newValue);

        // A deliberate change to the preference outranks whatever the
        // user did to the bar on this page earlier.
        if (settings.showControlBar !== previous.showControlBar) {
            hideOverride = null;
        }
        render();

        if (view.owned &&
            settings.intervalSeconds !== previous.intervalSeconds) {
            scheduleTick();
        }
    }

    //
    // Follow broadcast state pushed by the background, and the toolbar
    // button's request to show or hide the bar.
    //
    // Notes:
    //      The toolbar button flips whatever is on screen now, so it
    //      can bring the bar back on a page where it was dismissed
    //      and on a profile where the preference turned it off. That
    //      is the only way back for a user who hid it everywhere.
    //
    function onRuntimeMessage(message) {
        if (message?.type === MSG.STATE_CHANGED) {
            setView(message.view);
        } else if (message?.type === MSG.TOGGLE_UI) {
            hideOverride = !isHidden();
            render();
        }
    }

    //
    // Decide whether the bar is currently off screen.
    //
    // Returns:
    //      True when the bar should not be shown, taking the per-page
    //      override into account before the stored preference.
    //
    function isHidden() {
        return hideOverride ?? !settings.showControlBar;
    }

    //
    // Capture as soon as the tab comes back into view, rather than
    // waiting out the rest of a paused interval.
    //
    function onVisibilityChange() {
        if (view.owned && document.visibilityState === "visible") {
            scheduleTick(0);
        }
    }

    // ---------------------------------------------------------------
    // State and rendering
    // ---------------------------------------------------------------

    //
    // Adopt a new view of the broadcast and repaint.
    //
    // Parameters:
    //      next        - View object from the background, or null if
    //                    the message failed.
    //
    function setView(next) {
        if (!next) {
            return;
        }
        const wasOwned = view.owned;
        view = { ...view, ...next };

        // This tab owns a broadcast but has no clock running, which
        // is what a navigation looks like: the page reloaded, this
        // script is new, the session in the background is not.
        if (view.owned && !wasOwned && tickTimer === null) {
            scheduleTick(0);
        }
        if (!view.owned) {
            clearTick();
        }
        render();
    }

    //
    // Repaint the bar and keep the countdown timer in step with it.
    //
    function render() {
        const state = currentState();
        const live = state === "live";

        globalThis.TC_UI.paint(ui, {
            state,
            status: statusText(state),
            tooltip: view.lastError ?? "",
            intervalSeconds: settings.intervalSeconds,
            canStart: !busy && !view.broadcasting,
            canStop: !busy && view.owned,
            hidden: isHidden()
        });

        if (live && countdownTimer === null) {
            countdownTimer = setInterval(render, 1000);
        } else if (!live && countdownTimer !== null) {
            clearInterval(countdownTimer);
            countdownTimer = null;
        }
    }

    //
    // Classify the broadcast into the one word the bar is styled by.
    //
    // Returns:
    //      "idle", "foreign", "error", "paused", or "live".
    //
    function currentState() {
        if (!view.broadcasting) {
            return "idle";
        }
        if (!view.owned) {
            return "foreign";
        }
        if (view.lastError) {
            return "error";
        }
        if (view.paused || document.visibilityState !== "visible") {
            return "paused";
        }
        return "live";
    }

    //
    // Build the one line of status text.
    //
    function statusText(state) {
        switch (state) {
            case "idle":
                return "Idle";
            case "foreign":
                return "Another tab is broadcasting";
            case "error":
                return view.lastError;
            case "paused":
                return "Paused, this tab is not visible";
            default:
                return liveText();
        }
    }

    //
    // Describe a healthy broadcast: frames sent, readers reached, and
    // how long until the next capture.
    //
    function liveText() {
        const parts = [`Live, frame ${view.sequence}`];
        if (Number.isFinite(view.clients)) {
            parts.push(
                view.clients === 1 ? "1 reader" : `${view.clients} readers`
            );
        }
        if (tickDueAt > 0) {
            const remaining = Math.max(
                0,
                Math.round((tickDueAt - Date.now()) / 1000)
            );
            parts.push(`next in ${remaining}s`);
        }
        return parts.join(", ");
    }

    // ---------------------------------------------------------------
    // Plumbing
    // ---------------------------------------------------------------

    //
    // Send a message to the background context.
    //
    // Returns:
    //      Promise resolving to the reply, or to null when the
    //      background could not be reached. A torn down extension must
    //      not throw uncaught errors into the host page.
    //
    async function send(message) {
        try {
            return await api.runtime.sendMessage(message);
        } catch (error) {
            console.warn("[evangelist]", error);
            view = {
                ...view,
                lastError: "Evangelist background is not responding."
            };
            render();
            return null;
        }
    }

    //
    // Run an action with the buttons disabled, so a double click
    // cannot start two sessions.
    //
    async function withBusy(action) {
        busy = true;
        render();
        try {
            await action();
        } finally {
            busy = false;
            render();
        }
    }

    //
    // Attach the last resort handler to a promise. An unexpected
    // failure must leave the host page usable, so it stops the clock
    // and says so in the bar.
    //
    function guard(promise) {
        Promise.resolve(promise).catch((error) => {
            console.error("[evangelist]", error);
            clearTick();
            view = { ...view, lastError: String(error?.message ?? error) };
            render();
        });
    }

    //
    // Fill in and range check a settings object read from storage.
    //
    function normalise(raw) {
        const s = { ...DEFAULTS, ...(raw ?? {}) };
        return {
            ...s,
            intervalSeconds: clamp(
                s.intervalSeconds,
                LIMITS.intervalSeconds,
                DEFAULTS.intervalSeconds
            ),
            showControlBar: Boolean(s.showControlBar)
        };
    }

    //
    // Clamp a value into an inclusive range.
    //
    function clamp(value, range, fallback) {
        const n = Number(value);
        if (!Number.isFinite(n)) {
            return fallback;
        }
        return Math.min(range.max, Math.max(range.min, Math.round(n)));
    }
})();
