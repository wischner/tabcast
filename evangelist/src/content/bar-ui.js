//
// Evangelist control bar widget.
//
// Runs as a content script alongside overlay.js. Owns everything to do
// with the DOM: it builds the shadow root, wires the events to the
// handlers it is given, and paints a plain model object onto the
// elements. It holds no broadcast state and makes no decisions, so the
// look of the bar can be changed here without touching the capture
// logic.
//
// Classic script, not an ES module, for the reason given in
// common/constants.js. It publishes one `TC_UI` namespace.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

(function (root) {
    const { api, LIMITS, ROOT_ID } = root.TC;

    //
    // Build the control bar and attach it to the document.
    //
    // Parameters:
    //      handlers    - Object with `onStart`, `onStop`,
    //                    `onIntervalInput`, `onIntervalCommit` and
    //                    `onHide` callbacks. Each is called with no
    //                    arguments except onIntervalInput and
    //                    onIntervalCommit, which receive the slider
    //                    value as a number.
    //
    // Returns:
    //      A refs object to hand back to paint(). Callers must treat
    //      it as opaque.
    //
    // Notes:
    //      The stylesheet is loaded through runtime.getURL() and must
    //      stay listed in web_accessible_resources. A shadow root
    //      cannot see a stylesheet injected into the host document,
    //      and injecting ours into the host document is exactly what
    //      the shadow root exists to avoid.
    //
    function build(handlers) {
        const host = document.createElement("div");
        host.id = ROOT_ID;
        const shadow = host.attachShadow({ mode: "open" });

        const style = document.createElement("link");
        style.rel = "stylesheet";
        style.href = api.runtime.getURL("content/overlay.css");

        const bar = document.createElement("div");
        bar.className = "tc-bar";
        bar.dataset.state = "idle";
        bar.setAttribute("role", "region");
        bar.setAttribute("aria-label", "Evangelist broadcast controls");
        bar.innerHTML = markup();

        shadow.append(style, bar);
        (document.body ?? document.documentElement).append(host);

        const refs = {
            host,
            bar,
            start: bar.querySelector(".tc-start"),
            stop: bar.querySelector(".tc-stop"),
            slider: bar.querySelector(".tc-slider"),
            seconds: bar.querySelector(".tc-seconds"),
            status: bar.querySelector(".tc-status"),
            hide: bar.querySelector(".tc-hide")
        };

        refs.start.addEventListener("click", () => handlers.onStart());
        refs.stop.addEventListener("click", () => handlers.onStop());
        refs.hide.addEventListener("click", () => handlers.onHide());

        // "input" fires continuously while dragging, "change" only
        // when the handle is released. The first repaints the label,
        // the second is what actually commits a new interval.
        refs.slider.addEventListener("input", () =>
            handlers.onIntervalInput(Number(refs.slider.value))
        );
        refs.slider.addEventListener("change", () =>
            handlers.onIntervalCommit(Number(refs.slider.value))
        );

        return refs;
    }

    //
    // Paint a model onto the bar.
    //
    // Parameters:
    //      refs        - Object returned by build().
    //      model       - {
    //                        state,            one of idle, foreign,
    //                                          live, paused, error
    //                        status,           one line of text
    //                        tooltip,          hover text, may be ""
    //                        intervalSeconds,  slider position
    //                        canStart, canStop,
    //                        hidden            hide the whole bar
    //                    }
    //
    // Notes:
    //      Idempotent. Called on every state change and once a second
    //      while a broadcast is live, so it must stay cheap and must
    //      not fight the user for the slider handle.
    //
    function paint(refs, model) {
        refs.host.hidden = model.hidden;
        refs.bar.dataset.state = model.state;
        refs.start.disabled = !model.canStart;
        refs.stop.disabled = !model.canStop;
        refs.status.textContent = model.status;
        refs.status.title = model.tooltip ?? "";

        // Leave the handle alone while it has focus, which is the
        // case throughout a drag. Repainting it then would snap it
        // back from under the user's finger.
        if (refs.bar.getRootNode().activeElement !== refs.slider) {
            refs.slider.value = String(model.intervalSeconds);
        }
        setSeconds(refs, model.intervalSeconds);
    }

    //
    // Update only the seconds readout next to the slider.
    //
    // Parameters:
    //      refs        - Object returned by build().
    //      seconds     - Whole seconds to display.
    //
    // Notes:
    //      Separate from paint() because dragging the slider must
    //      show the new number immediately, long before anything is
    //      committed to storage.
    //
    function setSeconds(refs, seconds) {
        refs.seconds.textContent = `${seconds}s`;
    }

    //
    // Remove the bar from the page.
    //
    function destroy(refs) {
        refs.host.remove();
    }

    //
    // The bar's inner markup.
    //
    // Returns:
    //      HTML string. Nothing here interpolates page content, only
    //      our own numeric limits, so there is nothing for a hostile
    //      page to inject.
    //
    function markup() {
        return `
            <span class="tc-dot" aria-hidden="true"></span>
            <span class="tc-brand">Evangelist</span>
            <div class="tc-actions">
                <button type="button" class="tc-btn tc-start">
                    Broadcast
                </button>
                <button type="button" class="tc-btn tc-stop">Stop</button>
            </div>
            <label class="tc-interval">
                <span class="tc-label">Refresh</span>
                <input type="range" class="tc-slider"
                       min="${LIMITS.intervalSeconds.min}"
                       max="${LIMITS.intervalSeconds.max}"
                       step="1"
                       aria-label="Seconds between captures">
                <output class="tc-seconds">10s</output>
            </label>
            <span class="tc-status"></span>
            <button type="button" class="tc-hide" title="Hide the bar"
                    aria-label="Hide the bar">&times;</button>
        `;
    }

    root.TC_UI = Object.freeze({ build, paint, setSeconds, destroy });
})(globalThis);
