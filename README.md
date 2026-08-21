# Tabcast

Casts live web browser content onto e-ink tablets.

A page is captured in the browser, handed to a daemon on the local
machine, and fanned out to every e-ink reader on the network. The
intended use is a shared view: hymn and lyric sheets on a desk full of
readers, driven from one browser tab.

Three parts, named for how the message travels.

| Part | Name | What it does | Language |
| --- | --- | --- | --- |
| Browser extension | **Evangelist** | Sees the page and begins the proclamation. Adds a control bar to every page and captures the tab every N seconds. | JavaScript, MV3 |
| Broadcast daemon | **Apostle** | Is sent to carry the message to the whole community. Receives frames and distributes them to every registered reader. | Modern C++ |
| E-ink client | **Disciple** | Receives and displays the Word so the faithful can see. Multiple Disciples form the flock. | Modern C++, FreeInk SDK |

```
   Firefox / Chrome tab
   +---------------------------+
   |  page + Evangelist bar    |
   |  [Broadcast] [Stop] [10s] |
   +------------+--------------+
                |  capture every N seconds
                v
   +---------------------------+
   |  Apostle daemon           |  loopback ingest, LAN fan-out
   +--+---------+---------+----+
      |         |         |
      v         v         v
   Disciple  Disciple  test client
   XTEINK    XTEINK    on Linux
```

## Status

| Part | State |
| --- | --- |
| Evangelist | **Working.** Control bar, capture clock, and frame upload are implemented. |
| Apostle | **Runs, does not serve.** Console or daemon, syslog, pid file, signals, systemd unit. The listeners are not written. |
| Disciple | **Renders.** `--replay` shows a page exactly as the reader will, in a window. No networking yet. |

The wire protocol for both hops is specified in full in
[docs/PROTOCOL.md](docs/PROTOCOL.md), so neither daemon has to invent
it while being written.

## Build

Everything, from the repository root:

```sh
make                 # build all three parts
make check           # syntax check the extension, run the C++ tests
make BUILD=release   # release build of the C++ parts
make dist            # package the extension into bin/
make clean           # artifacts for the current build type
make distclean       # everything, leaving no build/ or bin/ behind
make help
```

Or one part at a time:

```sh
make evangelist
make apostle
make disciple
```

Each part is a self-contained project with its own `Makefile` and its
own `README.md`. The top level only orchestrates.

## Dependencies

Ubuntu or Debian:

```sh
sudo apt install build-essential make python3 libgtkmm-3.0-dev
```

- **Evangelist** needs `python3` and nothing else. No npm, no bundler,
  no network access at build time.
- **Apostle** and **Disciple** need `g++` with C++20 support and GNU
  Make. No cmake.
- **Disciple** picks its display at build time. With
  `libgtkmm-3.0-dev` installed it builds the windowed test client,
  `BACKEND=test`. Without it, it builds with no dependencies at all and
  still runs the panel model and the tests. The FreeInk SDK is for the
  real device and is not wired up yet.

Firefox or Chrome, to run the extension.

## Debugging with F5

In VS Code, **F5** runs the compound target **Tabcast: daemon + client**.
It builds all three parts, then launches Apostle and Disciple together
under gdb, with `stopAll` so closing one stops the other.

| | |
| --- | --- |
| Apostle | `--foreground --log-level debug`, so its log goes to the debug console rather than the journal. |
| Disciple | `--replay disciple/tests/data/sample-page.png`, because Apostle cannot broadcast yet. Drop the argument once it can. |

Both are debug builds, linked with the address and undefined behaviour
sanitizers, so a bad access stops in the debugger with a report rather
than corrupting the heap and failing later. Needs the C/C++ extension
(`ms-vscode.cpptools`) and `gdb`.

The build task also rebuilds the extension into `build/evangelist/`, so
after F5 you can reload it in the browser and it will be current. See
[evangelist/README.md](evangelist/README.md#install) for the reload
step, which Firefox does not do on its own.

**End to end works.** Press Broadcast in the browser and the page
appears in the Disciple window, fitted to the reader's portrait panel
and reduced to four grey levels. The path is Evangelist captures the
tab, posts it to Apostle over loopback, Apostle fans it out over TCP,
Disciple renders it and acknowledges.

What is still missing is the device itself: `BACKEND=freeink` is not
written, so the only Disciple that exists is the windowed test client
on a development machine. Host-side reduction to `gray-packed` is not
written either, so Apostle forwards the browser's PNG and lets the
client do the work. That is the right trade on a desktop and the wrong
one on a reader.

## Getting started

```sh
make
```

Then load the extension. Firefox needs one extra step that catches
everyone, so read
[evangelist/README.md](evangelist/README.md#install) rather than
guessing.

Apostle does not serve yet, so a broadcast started today will report
*Cannot reach Apostle at 127.0.0.1:8420*. That is the correct answer
until the daemon is written.

## Repository layout

```
tabcast/
├── Makefile              orchestrates the three parts
├── docs/
│   ├── ARCHITECTURE.md   how the parts fit together, and why
│   ├── PROTOCOL.md       both wire formats, in full
│   └── standards/        coding standards, binding on all agents
│       ├── CPP-CODING-STYLE.md
│       └── EXT-CODING-STYLE.md
├── evangelist/           browser extension
├── apostle/              broadcast daemon
├── disciple/             e-ink client
├── scripts/              developer automation
├── build/                every artifact, from every part
└── bin/                  every finished program
```

There is exactly one `build/` and one `bin/`, both here at the root.
No part writes anything beside its own sources, so `git status` on a
fully built tree is clean.

| | |
| --- | --- |
| `build/apostle/<type>/` | Objects, deps, intermediate links. `<type>` is `debug` or `release`. |
| `build/disciple/<type>/` | The same. |
| `build/evangelist/<browser>/` | Unpacked extension. This is what you load with "load unpacked". |
| `bin/apostle`, `bin/disciple` | The finished programs. |
| `bin/evangelist-<version>-<browser>.xpi` | Packaged extension, written by `make dist`. |

Building from inside a part works the same way. `cd apostle && make`
puts its output in the root `build/` and `bin/`, not in `apostle/`.

## Standards

`docs/standards/` is normative, for human and AI contributors alike.
Read the relevant one before writing code here:

- [CPP-CODING-STYLE.md](docs/standards/CPP-CODING-STYLE.md) for
  Apostle and Disciple. Layout, naming, file headers, and the build
  rules, including why release builds link the C++ runtime in but are
  not fully static.
- [EXT-CODING-STYLE.md](docs/standards/EXT-CODING-STYLE.md) for
  Evangelist. The no-toolchain rule, the cross-browser rules, and the
  one place a classic script is allowed instead of an ES module.

## Licence

MIT. See [LICENSE](LICENSE).
