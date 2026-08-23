# Evangelist

The Tabcast browser extension. It sees the page and begins the
proclamation: while broadcasting, it photographs the visible tab every
few seconds and hands each frame to the Apostle daemon, which carries
it to every e-ink reader in the flock.

Firefox and Chrome, Manifest V3, one source tree.

## Build

The only dependency is `python3`. No npm, no bundler, no network.

```sh
make            # unpacked trees plus loadable archives, see below
make dist       # same packaged build, for distribution workflows
make check      # syntax check the manifests and the scripts
make clean
```

Output goes to the repository root, not into this directory. There is
one `build/` and one `bin/` for all three parts of Tabcast:

| What | Where |
| --- | --- |
| Unpacked Firefox tree, load this while working | `../build/evangelist/firefox/` |
| Unpacked Chrome tree, load this while working | `../build/evangelist/chrome/` |
| Signed-and-shipped archives, from `make` or `make dist` | `../bin/evangelist-<version>-firefox.xpi` and `-chrome.zip` |

## Install

### Firefox

1. `make firefox`
2. Open `about:debugging#/runtime/this-firefox`.
3. **Load Temporary Add-on**, and pick
   `build/evangelist/firefox/manifest.json` at the repository root.
4. **This step is not optional.** Open `about:addons`, click
   Evangelist, then Permissions, and turn on *Access your data for all
   websites*.

Firefox treats Manifest V3 host permissions as optional and does not
prompt for them at install time. Without step 4 the control bar
appears and the Broadcast button reports **captureVisibleTab is not a
function**, because Firefox does not inject the method at all until
the `<all_urls>` permission it is gated on has actually been granted.
This surprises everyone once.

A temporary add-on is removed when Firefox closes. For a permanent
install, sign the `.xpi` from `make dist` through addons.mozilla.org,
or run Firefox Developer Edition or Nightly with
`xpinstall.signatures.required` set to false.

### Chrome

1. `make chrome`
2. Open `chrome://extensions`.
3. Turn on **Developer mode**.
4. **Load unpacked**, and pick `build/evangelist/chrome` at the
   repository root.

Chrome grants the declared host permissions at install time, so there
is no extra step.

## Use

Every http and https page gets a control bar in the bottom right
corner.

- **Broadcast** starts capturing this tab. The first frame goes out
  immediately, so a daemon that is down is reported at once rather
  than after a full interval.
- **Stop** ends the broadcast.
- The **Refresh** slider sets the seconds between captures, from 2 to
  300. It takes effect immediately, including mid-broadcast.
- The **countdown ring** shows the seconds until the next capture and
  drains as they run down. While a frame is actually being sent it
  turns into a spinner, because at that point there is nothing left to
  count. It appears only while this tab is broadcasting.
- The status line reads *Live, frame 12, 3 readers*, or the reason
  nothing is being sent.
- **×** hides the bar on this page. The toolbar button brings it back.

The bar widens to fit its status message and, on a viewport narrower
than 900px, moves the message to a second line rather than truncating
it.

The toolbar button shows and hides the bar. The badge reads `ON` while
a broadcast is running.

Settings live in the browser's add-on manager, under Evangelist. The
daemon host and port, the capture format, and the two behaviour
switches are all there, along with a **Test connection** button that
reports whether Apostle is answering.

## How it works

The capture clock runs in the content script, not in the background.
A Manifest V3 background context is torn down when idle and its timers
die with it, whereas a page keeps its timers for as long as it is
worth capturing. Each tick, the content script asks the background to
photograph and send, which wakes the background if it had been shut
down.

The background does the capture and the upload because a content
script can do neither: `tabs.captureVisibleTab()` is not exposed to
content scripts, and a content script's `fetch()` is subject to the
host page's CORS policy.

See `docs/ARCHITECTURE.md` and `docs/PROTOCOL.md` at the repository
root.

## Permissions

| Permission | Why |
| --- | --- |
| `storage` | Holds the settings, and the state of the running broadcast. |
| `http://*/*`, `https://*/*` | Two things at once. `tabs.captureVisibleTab()` requires a host permission for the page being photographed, and the frame upload to `http://127.0.0.1:8420` needs one for loopback. |

There is no `tabs` permission. The page title and URL that travel with
a frame come from the content script's own `document`, not from the
tabs API, so the extension never gains the ability to read the URL of
a tab it is not running in.

Captured page content goes to the configured loopback daemon and
nowhere else. There is no analytics, no telemetry, and no remote code.

## Limitations

**One tab at a time.** `captureVisibleTab()` can only photograph the
tab the user is actually looking at, so only one broadcast runs at
once. Starting one in a second tab stops the first, and the displaced
tab's bar says so.

**Capture pauses when the tab is hidden.** Switch tab or minimise the
window and the status reads *Paused, this tab is not visible*. Nothing
is sent, because what would be sent is the wrong page. It resumes the
moment the tab comes back.

**Unchanged frames are dropped**, unless you turn that off in the
settings. A static page produces no traffic and no panel refresh.

**Not every page can carry the bar.** `about:`, `chrome://`, the
add-on store, PDF viewers, and the new tab page all refuse content
scripts. On those pages the toolbar button opens the settings instead.

## Layout

```
evangelist/
├── Makefile
├── src/
│   ├── manifest.json           shared Manifest V3 base
│   ├── manifest.firefox.json   Firefox overrides, add-on id
│   ├── manifest.chrome.json    Chrome overrides
│   ├── common/
│   │   ├── constants.js        classic script, the TC namespace
│   │   ├── shared.js           ES module face of the above
│   │   └── config.js           settings storage
│   ├── background/
│   │   ├── background.js       message router, entry point
│   │   ├── session.js          broadcast state
│   │   ├── frame-sender.js     capture, hash, upload
│   │   └── image-info.js       PNG and JPEG header reader
│   ├── content/
│   │   ├── bar-ui.js           the bar's DOM, no state
│   │   ├── overlay.js          the capture clock and state
│   │   └── overlay.css
│   ├── options/
│   └── icons/                  generated, see tools/make_icons.py
└── tools/
    ├── build.py                merge manifests, copy, package
    └── make_icons.py           draw the icon set
```

Nothing is generated inside this directory. The icons under
`src/icons/` are the one committed exception, because they are drawn
once by `tools/make_icons.py` and then treated as sources.

Coding rules for this project are in
`docs/standards/EXT-CODING-STYLE.md`.
