# EXT-CODING-STYLE.md - Browser Extension Standards and Project Rules

This document defines the rules that all agents, including AI agents,
must follow when writing, reviewing, or modifying browser extension
code in this project.

We target Firefox and Chrome from a single source tree using
Manifest V3.

- All development happens on Ubuntu/Debian Linux.
- Firefox is the primary target. Chrome must keep working.

## 1. No Toolchain

This is a hard rule.

- No `npm`, no `node_modules`, no bundler, no transpiler.
- No framework. Plain HTML, plain CSS, plain ES modules.
- The only build dependency is `python3`, which Ubuntu and Debian
  already ship.
- A checkout must be loadable as an unpacked extension after one
  `make` with no network access.

If a task seems to need a bundler, the task is wrong, not the rule.

## 2. Project Directory Structure

Do not create files outside the approved directories.

- `src/` contains everything that ships inside the extension.
- `src/manifest.json` is the shared Manifest V3 base.
- `src/manifest.<browser>.json` holds per-browser overrides only.
- `src/common/` contains modules shared by all contexts.
- `src/common/constants.js` is the one classic script, see section 7.
- `src/background/` contains the background service worker or event
  page.
- `src/content/` contains content scripts and their stylesheets.
- `src/options/` contains the options page.
- `src/icons/` contains generated icon assets.
- `tools/` contains build and asset scripts.
- `build/` contains all build output, including unpacked per-browser
  trees and packaged archives.

Rules:

- Build output must go only into the project `build/` directory.
- Never edit anything under `build/`. It is disposable.
- Generated assets such as icons must have their generator committed
  in `tools/`.

## 3. Naming Conventions

- Use lowercase kebab-case for file names: `frame-sender.js`.
- Use lowerCamelCase for JavaScript variables and functions.
- Use UPPER_SNAKE_CASE for module level constants.
- Use PascalCase only for classes.
- Prefix CSS class names in injected UI with `tc-` to avoid collisions
  with the host page.

Examples:

- Good: `sendFrame()`, `intervalSeconds`, `DEFAULT_PORT`, `tc-toolbar`
- Bad: `SendFrame()`, `interval_seconds`, `defaultPort`, `toolbar`

## 4. File Header

Every `.js`, `.css`, and `.html` file must begin with a real file
header in this style:

```js
//
// Describe exactly what this file does.
// Be specific about the module purpose, which extension context it
// runs in, and any browser differences that matter.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//
```

Rules:

- Replace the placeholder text with a real description.
- Always name the context the file runs in: background, content
  script, options page, or build tool.
- Keep lines readable on a 72-column terminal where practical.

## 5. Function Documentation

Every exported function must have a comment immediately above it.

```js
//
// Post a single captured frame to the Apostle daemon.
//
// Parameters:
//      blob        - PNG or JPEG image data for one frame.
//      settings    - Resolved settings object, see config.js.
//
// Returns:
//      Promise resolving to the daemon reply, or rejecting with an
//      Error whose message is safe to show in the UI.
//
// Notes:
//      Must run in the background context. Content scripts are
//      subject to page CORS and will fail.
//
export async function sendFrame(blob, settings) {
```

Rules:

- Say what the function does, not how.
- State which extension context the function may run in when it
  matters.
- Document non-obvious private helpers too.

## 6. Cross-Browser Rules

- Never call `chrome.*` or `browser.*` directly. Use the `api` shim
  from `src/common/shared.js`, or `TC.api` in a content script.
- Use promise-returning API calls only. No callback style.
- Keep every browser difference in either `src/common/constants.js` or
  a `manifest.<browser>.json` override. Nothing else may branch on the
  browser.
- Firefox requires `browser_specific_settings.gecko.id`. Chrome
  rejects that key, so it lives in the Firefox override only.
- Firefox MV3 uses a background `scripts` event page. Chrome MV3 uses
  a `service_worker`. Assume the background can be torn down at any
  moment and never keep state only in memory.

## 7. Modules and Coding Rules

Use ES modules with explicit `import` and `export` everywhere except
content scripts.

Content scripts cannot be ES modules under Manifest V3, in either
browser. That is a platform limit, not a preference, and it forces one
narrow exception:

- `src/common/constants.js` is a classic script. It wraps its body in
  an IIFE and freezes a single `TC` namespace onto `globalThis`.
- Content scripts read `globalThis.TC` and are listed in the manifest
  in load order, sharing the isolated world's globals.
- Module contexts never read that global. They import
  `src/common/shared.js`, which re-exports `TC` as named bindings.
- Any further content script file follows the same pattern: an IIFE
  publishing one namespace, listed before its consumers.

Do not work around this with dynamic `import()` of a web-accessible
module. It is unevenly supported and it puts our source on a URL any
page can probe.

The rest:

- Every file is strict mode by virtue of being a module or an IIFE.
  Do not add `"use strict"`.
- Use `const` by default and `let` when reassignment is real. Never
  use `var`.
- Prefer small, focused functions.
- Keep files under about 300 lines of code, counting neither the file
  header nor the documentation comments. A file that has to be split
  should be split along a real seam, such as DOM construction against
  behaviour, and never into arbitrary halves.
- No global mutable state outside a single clearly marked module.
- Use 4 spaces for indentation and no tabs.
- Keep code readable on a 72-column terminal.
- Handle every rejected promise. A silent `catch {}` is a bug.

## 8. Permissions and Privacy

- Request the minimum permission set that makes the feature work.
- Every entry in `permissions` and `host_permissions` must be
  justified in the extension `README.md`.
- Captured page content is user data. It goes to the configured
  loopback daemon and nowhere else.
- No analytics, no telemetry, no remote code loading. Manifest V3
  forbids remote code and so do we.

## 9. Injected UI

- Injected page UI must live inside a shadow root so host page CSS
  cannot reach it and our CSS cannot reach the host page.
- Load injected stylesheets through `runtime.getURL()` and declare
  them in `web_accessible_resources`.
- Never modify the host page DOM outside our own container element.
- Respect `prefers-color-scheme` and `prefers-reduced-motion`.

## 10. Build and Packaging

- `make` produces an unpacked tree per browser under `build/`.
- `make dist` produces loadable archives under `build/`.
- The version number lives in `src/manifest.json` and nowhere else.
- Packaged archives must contain no source maps and no build tools.

## 11. Documentation

- Keep the extension `README.md` up to date.
- `README.md` must explain how to build, how to side-load in both
  browsers, the permission justification, and known limitations.
