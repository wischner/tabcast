# Tabcast Architecture

Tabcast casts live web browser content onto e-ink tablets.

A page is captured in the browser, handed to a local daemon, and fanned
out to every registered e-ink reader on the network.

## The three parts

| Part | Name | Language | Runs on |
| --- | --- | --- | --- |
| Browser extension | **Evangelist** | JavaScript, MV3 | Firefox, Chrome |
| Broadcast daemon | **Apostle** | Modern C++ | Ubuntu Linux |
| E-ink client | **Disciple** | Modern C++, FreeInk SDK | XTEINK X4 |

**Evangelist** sees the page and begins the proclamation. It adds a
control bar to every page with start, stop, and a refresh interval
slider. While broadcasting it captures the visible tab every N seconds
and posts the image to Apostle.

**Apostle** is sent to carry the message to the whole community. It
accepts frames from Evangelist on loopback, keeps the newest frame, and
distributes it to every registered Disciple plus any test client.

**Disciple** receives and displays the Word. It registers with Apostle,
receives frames, and renders them on the e-ink panel. Multiple
Disciples form the flock.

Its display is chosen at build time: a window on a development machine
through gtkmm, the real panel through the FreeInk SDK, or nothing at
all. The reduction to panel size and grey depth is shared by all three,
so the test client shows exactly what the device will.

## Data flow

```
   Firefox / Chrome tab
   +---------------------------+
   |  page + Evangelist bar    |
   |  [start] [stop] [ 10s ]   |
   +------------+--------------+
                |  captureVisibleTab() every N seconds
                v
   +---------------------------+
   |  Evangelist background    |
   +------------+--------------+
                |  HTTP POST /api/v1/frame   (loopback only)
                v
   +---------------------------+
   |  Apostle daemon           |  holds the current frame
   +--+---------+---------+----+
      |         |         |
      v         v         v
   Disciple  Disciple  test client       (LAN, framed TCP)
   XTEINK    XTEINK    on Linux
```

## Design decisions

**One broadcasting tab at a time.** Frame capture uses
`tabs.captureVisibleTab()`, which can only photograph the tab the user
is actually looking at. Starting a broadcast in a second tab stops the
first one. This matches the intended use, where one machine drives one
shared view.

**Capture pauses when the tab is hidden.** If the user switches tab or
minimises the window, there is nothing to capture, so Evangelist stops
sending and reports `paused` rather than pushing a stale or wrong
image. It resumes automatically when the tab becomes visible again.

**Unchanged frames are dropped.** E-ink panels are slow and every
refresh is visible to the reader. Evangelist hashes each capture and
skips the upload when nothing changed, so a static page produces no
traffic and no panel flicker.

**Loopback only.** Evangelist talks to `127.0.0.1` and never to the
open network. Apostle owns all LAN exposure, which keeps exactly one
component in the security-relevant position.

**Push, not poll.** Apostle pushes to Disciples so a battery-powered
reader can idle on a socket rather than wake on a timer.

## Repository layout

```
tabcast/
├── Makefile              top level orchestration
├── docs/
│   ├── ARCHITECTURE.md   this file
│   ├── PROTOCOL.md       wire formats
│   └── standards/        coding standards, binding on all agents
├── evangelist/           browser extension
├── apostle/              broadcast daemon
├── disciple/             e-ink client
├── scripts/              developer automation
├── build/                every artifact, from every part
└── bin/                  every finished program
```

Each of the three parts is a self-contained project with its own
`Makefile` and its own `README.md`. The top level `Makefile` only
orchestrates and defines no compile rules of its own.

Output is the one thing the parts share. There is a single `build/`
and a single `bin/` at the root, and each part writes into its own
subdirectory of `build/` while dropping finished programs straight
into `bin/`. Nothing is generated beside a part's sources, which is
what keeps `git status` clean on a fully built tree and makes
`make distclean` a complete undo.

Because `bin/` is shared, each part's `distclean` deletes its own
program by name rather than emptying the directory. Cleaning Apostle
must not remove Disciple.

## Status

- Evangelist: control bar, capture loop, and frame upload implemented.
- Apostle: done for v1. It runs in the foreground or as a detached
  daemon, serves the loopback ingest and health endpoints, and fans
  every accepted frame out to the registered Disciples over TCP, one
  frame in flight per client. Host-side reduction to `gray-packed` is
  not written: frames go on as the browser encoded them.
- Disciple: the test client is done. It joins an Apostle, receives
  broadcast frames, fits each page to 480 x 800, reduces it to four
  grey levels, and shows it in a gtkmm window, acknowledging each one
  so the daemon can pace itself. No FreeInk yet, so there is no
  Disciple that runs on a reader.
