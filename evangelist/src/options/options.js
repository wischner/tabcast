//
// Evangelist settings page logic.
//
// Runs on the options page. Binds the form to the settings object in
// storage.local and autosaves on every change, so this page and the
// on-page refresh slider never disagree about who owns a value.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import { api, DEFAULTS, MSG, SETTINGS_KEY } from "../common/shared.js";
import { loadSettings, saveSettings } from "../common/config.js";

// How long the "Saved" note stays up.
const SAVED_NOTE_MS = 1600;

const el = {
    host: document.getElementById("host"),
    port: document.getElementById("port"),
    probe: document.getElementById("probe"),
    probeResult: document.getElementById("probe-result"),
    interval: document.getElementById("interval"),
    intervalValue: document.getElementById("interval-value"),
    format: document.getElementById("format"),
    quality: document.getElementById("quality"),
    qualityValue: document.getElementById("quality-value"),
    qualityField: document.getElementById("quality-field"),
    skipUnchanged: document.getElementById("skip-unchanged"),
    showBar: document.getElementById("show-bar"),
    reset: document.getElementById("reset"),
    saved: document.getElementById("saved")
};

let savedNoteTimer = null;

//
// Read the current settings and put them in the form.
//
// Parameters:
//      settings    - Validated settings object.
//
function fillForm(settings) {
    el.host.value = settings.host;
    el.port.value = String(settings.port);
    el.interval.value = String(settings.intervalSeconds);
    el.format.value = settings.format;
    el.quality.value = String(settings.jpegQuality);
    el.skipUnchanged.checked = settings.skipUnchanged;
    el.showBar.checked = settings.showControlBar;
    refreshDerived();
}

//
// Update the parts of the page that follow from other controls: the
// live readouts, and whether JPEG quality applies at all.
//
function refreshDerived() {
    el.intervalValue.textContent = `${el.interval.value}s`;
    el.qualityValue.textContent = el.quality.value;
    el.qualityField.hidden = el.format.value !== "jpeg";
}

//
// Collect the form into a settings patch.
//
// Returns:
//      A settings object. Values are not range checked here because
//      saveSettings() normalises everything it is given.
//
function readForm() {
    return {
        host: el.host.value,
        port: el.port.value,
        intervalSeconds: el.interval.value,
        format: el.format.value,
        jpegQuality: el.quality.value,
        skipUnchanged: el.skipUnchanged.checked,
        showControlBar: el.showBar.checked
    };
}

//
// Save the form and confirm it on screen.
//
// Notes:
//      The write is echoed back into the form because saveSettings()
//      clamps out-of-range values, and a port of 99999 silently
//      becoming 65535 with no visible correction would be worse than
//      rejecting it.
//
async function save() {
    const settings = await saveSettings(readForm());
    fillForm(settings);
    note("Saved");
}

//
// Flash a short message in the footer.
//
// Parameters:
//      text        - Message to show, cleared after a moment.
//
function note(text) {
    el.saved.textContent = text;
    if (savedNoteTimer !== null) {
        clearTimeout(savedNoteTimer);
    }
    savedNoteTimer = setTimeout(() => {
        el.saved.textContent = "";
        savedNoteTimer = null;
    }, SAVED_NOTE_MS);
}

//
// Ask the background context whether Apostle answers, and report it.
//
// Notes:
//      The probe runs in the background rather than here because the
//      options page is a normal document subject to CORS, while the
//      background context has the loopback host permission.
//
async function probe() {
    el.probe.disabled = true;
    el.probeResult.dataset.state = "busy";
    el.probeResult.textContent = "Checking...";

    try {
        const settings = await saveSettings(readForm());
        fillForm(settings);

        const result = await api.runtime.sendMessage({ type: MSG.PROBE });
        if (result?.ok) {
            const info = result.info ?? {};
            const version = info.version ? ` ${info.version}` : "";
            const clients = Number.isFinite(info.clients)
                ? `, ${info.clients} reader(s) connected`
                : "";
            el.probeResult.dataset.state = "ok";
            el.probeResult.textContent = `Apostle${version} is up${clients}.`;
        } else {
            el.probeResult.dataset.state = "bad";
            el.probeResult.textContent =
                result?.error ?? "No answer from Apostle.";
        }
    } catch (error) {
        el.probeResult.dataset.state = "bad";
        el.probeResult.textContent = String(error?.message ?? error);
    } finally {
        el.probe.disabled = false;
    }
}

//
// Put every setting back to its factory value.
//
async function restoreDefaults() {
    fillForm(await saveSettings({ ...DEFAULTS }));
    note("Defaults restored");
}

//
// Attach a handler that saves, reporting any failure on the page
// rather than only in the console.
//
// Parameters:
//      element     - Control to listen on.
//      event       - Event name, "input" for live controls and
//                    "change" for the rest.
//
function autosave(element, event) {
    element.addEventListener(event, () => {
        refreshDerived();
        save().catch((error) => note(String(error?.message ?? error)));
    });
}

// Sliders repaint their readout on every movement but only commit on
// release, so the storage write does not fire once per pixel.
el.interval.addEventListener("input", refreshDerived);
el.quality.addEventListener("input", refreshDerived);

autosave(el.host, "change");
autosave(el.port, "change");
autosave(el.interval, "change");
autosave(el.quality, "change");
autosave(el.format, "change");
autosave(el.skipUnchanged, "change");
autosave(el.showBar, "change");

el.probe.addEventListener("click", () => {
    probe().catch((error) => note(String(error?.message ?? error)));
});
el.reset.addEventListener("click", () => {
    restoreDefaults().catch((error) =>
        note(String(error?.message ?? error))
    );
});

// Follow changes made elsewhere, most often the refresh slider on a
// page's control bar, so this form is never stale.
api.storage.onChanged.addListener((changes, area) => {
    if (area === "local" && changes[SETTINGS_KEY]) {
        fillForm({ ...DEFAULTS, ...(changes[SETTINGS_KEY].newValue ?? {}) });
    }
});

fillForm(await loadSettings());
