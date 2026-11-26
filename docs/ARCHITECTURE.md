# Tabcast Architecture

Tabcast casts live web browser content onto e-ink tablets.

A page is captured in the browser, handed to a local daemon, and fanned
out to every registered e-ink reader on the network.

## The three parts

| Part | Name | Language | Runs on |
| --- | --- | --- | --- |
| Browser extension | **Evangelist** | JavaScript, MV3 | Firefox, Chrome |
| Broadcast daemon | **Apostle** | Modern C++ | Ubuntu Linux |
| E-ink client | **Disciple** | Modern C++, on inkerface (FreeInk) | XTEINK X4, any inkerface reader |

**Evangelist** sees the page and begins the proclamation. It adds a
control bar to every page with start, stop, and a refresh interval
slider. While broadcasting it captures the visible tab every N seconds
and posts the image to Apostle.

**Apostle** is sent to carry the message to the whole community. It
accepts frames from Evangelist on loopback, keeps the newest frame, and
distributes it to every registered Disciple.

**Disciple** receives and displays the Word. It registers with Apostle,
receives frames, and renders them on the e-ink panel. Multiple
Disciples form the flock.

Disciple is an inkerface program and nothing else. inkerface (the
sibling repository `../inkerface`) is the e-ink platform, directly on
the FreeInk SDK: it drives the panel, buttons, Wi-Fi, and storage on
every reader it supports, and emulates the X4 on a desktop with the
same FreeInk renderer. Disciple uses inkerface's types directly, with no
layer of its own between them, so it runs unchanged on any of those
readers and in the emulation. Where Disciple needed something inkerface
lacked (a soft access point, a network scan, listening sockets, UDP
broadcast), inkerface was extended rather than worked around. The panel
size comes from the display at run time, so each reader asks Apostle
for pages sized for its own panel.

Apostle reduces each page to the panel a reader describes in its hello:
fitted, quantised to its grey levels, and packed (`image/gray-packed`).
A reader therefore needs no image decoder and no memory for a colour
page, which a microcontroller does not have.

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
   Disciple  Disciple  Disciple          (LAN, framed TCP;
                                          found by UDP discovery)
   XTEINK X4 any       inkerface's
             inkerface X4 emulation
             reader    on Linux
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

**A phone is the reader's keyboard.** A reader out of the box knows
no Wi-Fi network. It becomes an access point named `sheep_` and eight
hex digits, shows the password on its panel, and serves a setup page
that a joining phone opens by itself through a captive portal. The
network chosen there is saved once it has been joined.

**Readers find Apostle themselves.** A reader has no keyboard, so it
is never told an address. On power-up it shows a splash, then
broadcasts a UDP `discover` and joins whichever Apostle answers. It
reconnects the same way whenever the link is lost, and keeps the last
page on the panel while it does.

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
  daemon, serves the loopback ingest and health endpoints, answers
  UDP discovery, and fans every accepted frame out to the registered
  Disciples over TCP, one frame in flight per client. Readers that
  accept `gray-packed` get each page reduced for their panel, decoded
  once and reduced once per distinct panel.
- Disciple: an inkerface program, working end to end in inkerface's
  desktop emulation of the X4, and building as X4 firmware. On power-up
  it shows a splash, gets onto Wi-Fi (a phone sets it up through the
  reader's own `sheep_` access point), finds Apostle by UDP discovery,
  joins it, and shows every page, reconnecting by itself when the link
  is lost. Runs on a real X4; holding the power button switches it off.