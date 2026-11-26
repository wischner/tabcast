//
// Content-script capture lifecycle regression tests, run under Node.
// Fake browser messages and paint callbacks exercise the real overlay
// script without a browser profile or third-party dependencies.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = (path) => readFileSync(new URL(path, import.meta.url), "utf8");
const settle = () => new Promise((resolve) => setImmediate(resolve));

async function page(showControlBar = true) {
    let listener, model, pending, repaint, visibilityChanged;
    let captures = 0;
    let nextId = 0;
    const timers = new Map();
    const frames = new Map();
    const state = { broadcasting: true, owned: true, sequence: 0 };
    const browser = {
        storage: {
            local: { get: async () => ({ settings: { showControlBar } }) },
            onChanged: { addListener() {} }
        },
        runtime: {
            onMessage: { addListener(fn) { listener = fn; } },
            sendMessage(message) {
                if (message.type === "state:get") {
                    return Promise.resolve(state);
                }
                assert.equal(message.type, "frame:tick");
                assert.equal(model.hidden, true, "hide before capturing");
                captures += 1;
                return new Promise((resolve, reject) => {
                    pending = { resolve, reject };
                });
            }
        }
    };
    const document = {
        visibilityState: "visible",
        title: "Test page",
        getElementById() { return null; },
        addEventListener(event, fn) { visibilityChanged = fn; }
    };
    const context = vm.createContext({
        browser, document,
        location: { href: "https://example.test/" },
        console: { warn() {}, error(error) { throw error; } },
        TC_UI: { build: () => ({}), paint(refs, next) { model = next; } },
        setTimeout(fn, delay) {
            timers.set(++nextId, { fn, delay });
            return nextId;
        },
        clearTimeout(id) { timers.delete(id); },
        setInterval(fn) { repaint = fn; return ++nextId; },
        clearInterval() {},
        requestAnimationFrame(fn) { frames.set(++nextId, fn); return nextId; },
        cancelAnimationFrame(id) { frames.delete(id); }
    });
    vm.runInContext(source("../src/common/constants.js"), context);
    vm.runInContext(source("../src/content/overlay.js"), context);
    await settle();
    return {
        get hidden() { return model.hidden; },
        get captures() { return captures; },
        message(type) { listener({ type }); },
        repaint() { repaint(); },
        visibility(value) {
            document.visibilityState = value;
            visibilityChanged();
        },
        async timer(delay) {
            const entry = [...timers].find(([, timer]) => timer.delay === delay);
            assert.ok(entry, `expected timer with delay ${delay}`);
            timers.delete(entry[0]);
            entry[1].fn();
            await settle();
        },
        async frame() {
            const ready = [...frames.values()];
            frames.clear();
            ready.forEach((fn) => fn());
            await settle();
        },
        async complete(error) {
            if (error) {
                pending.reject(error);
            } else {
                pending.resolve({ ok: true, view: state });
            }
            await settle();
        }
    };
}

test("hide through painting and countdown updates; restore before upload", async () => {
    const tab = await page();
    await tab.timer(0);
    assert.equal(tab.hidden, true);
    await tab.frame();
    assert.equal(tab.captures, 0);
    await tab.frame();
    assert.equal(tab.captures, 1);
    tab.repaint();
    assert.equal(tab.hidden, true);
    // A visibility event must not create a second overlapping capture.
    tab.visibility("visible");
    await tab.timer(0);
    assert.equal(tab.captures, 1);
    tab.message("frame:captured");
    assert.equal(tab.hidden, false);
    await tab.complete();
    assert.equal(tab.hidden, false);
});

test("a rejected background message restores the controls", async () => {
    const tab = await page();
    await tab.timer(0);
    await tab.frame();
    await tab.frame();
    await tab.complete(new Error("background unavailable"));
    assert.equal(tab.hidden, false);
});

test("restoration respects an already hidden bar", async () => {
    const tab = await page(false);
    await tab.timer(0);
    await tab.frame();
    await tab.frame();
    tab.message("frame:captured");
    assert.equal(tab.hidden, true);
    await tab.complete();
    assert.equal(tab.hidden, true);
});

test("suspended animation callbacks time out without taking a screenshot", async () => {
    const tab = await page();
    await tab.timer(0);
    await tab.timer(1000);
    assert.equal(tab.hidden, false);
    assert.equal(tab.captures, 0);
});

test("switching away during paint skips capture and restores controls", async () => {
    const tab = await page();
    await tab.timer(0);
    tab.visibility("hidden");
    await tab.frame();
    await tab.frame();
    assert.equal(tab.hidden, false);
    assert.equal(tab.captures, 0);
});
