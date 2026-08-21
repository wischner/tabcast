//
// Shared constants for every Evangelist context: message vocabulary,
// default settings, and the accepted value ranges.
//
// This file is a CLASSIC script, not an ES module. Manifest V3 content
// scripts cannot be ES modules, and the content script needs these
// values, so the one shared file that must reach it cannot use export
// syntax. It freezes a single `TC` namespace onto globalThis instead.
// ES module contexts must not read that global directly. They import
// common/shared.js, which re-exports it as proper named bindings.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

(function (root) {
    //
    // Message types exchanged between the content script, the
    // background context, and the options page. Every runtime message
    // is an object with a `type` field taken from here.
    //
    const MSG = {
        // content or options -> background, asks for current state.
        GET_STATE: "state:get",
        // background -> content, pushed whenever state changes.
        STATE_CHANGED: "state:changed",
        // content -> background, begin broadcasting this tab.
        START: "broadcast:start",
        // content -> background, stop broadcasting.
        STOP: "broadcast:stop",
        // content -> background, capture and send one frame now.
        TICK: "frame:tick",
        // background -> content, show or hide the control bar.
        TOGGLE_UI: "ui:toggle",
        // options -> background, check whether Apostle answers.
        PROBE: "daemon:probe"
    };

    //
    // Factory settings. Anything the options page can change lives
    // here and nowhere else.
    //
    const DEFAULTS = {
        // Seconds between captures while broadcasting.
        intervalSeconds: 10,
        // Where the Apostle daemon listens. Loopback only by design.
        host: "127.0.0.1",
        port: 8420,
        // Capture format. PNG is right for e-ink, JPEG saves bytes.
        format: "png",
        jpegQuality: 80,
        // Do not resend a frame identical to the previous one. Saves
        // a visible, slow panel refresh on every reader.
        skipUnchanged: true,
        // Show the control bar on pages.
        showControlBar: true
    };

    //
    // Accepted ranges. The options page and the control bar slider
    // both clamp to these, so a hand-edited storage value cannot put
    // the extension into an impossible state.
    //
    const LIMITS = {
        intervalSeconds: { min: 2, max: 300 },
        port: { min: 1, max: 65535 },
        jpegQuality: { min: 10, max: 100 },
        formats: ["png", "jpeg"]
    };

    root.TC = Object.freeze({
        MSG: Object.freeze(MSG),
        DEFAULTS: Object.freeze(DEFAULTS),
        LIMITS: Object.freeze(LIMITS),
        // storage.local key holding the settings object.
        SETTINGS_KEY: "settings",
        // Root element id of the injected control bar.
        ROOT_ID: "tabcast-evangelist-root",
        // The one cross-browser extension API entry point. Firefox
        // exposes promise based `browser`, Chrome exposes `chrome`,
        // which is promise based too under Manifest V3.
        api: root.browser ?? root.chrome
    });
})(globalThis);
