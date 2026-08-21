//
// ES module face of common/constants.js.
//
// Runs in the background context and on the options page. Imports the
// classic constants script for its side effect, then re-exports the
// frozen `TC` namespace as ordinary named bindings so module code
// never has to touch a global.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

import "./constants.js";

export const { MSG, DEFAULTS, LIMITS, SETTINGS_KEY, ROOT_ID, api } =
    globalThis.TC;
