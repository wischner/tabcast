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
| Apostle | **Working.** Receives frames over HTTP and broadcasts them to connected readers over TCP. |
| Disciple | **Working desktop client.** Receives live frames in a window; `--replay` previews a local image. The device backend is not written yet. |

The wire protocol for both hops is specified in full in
[docs/PROTOCOL.md](docs/PROTOCOL.md), so neither daemon has to invent
it while being written.

## Build

Everything, from the repository root:

```sh
make                 # build all three parts into build/ and bin/
make check           # syntax check the extension, run the C++ tests
make BUILD=release   # release build of the C++ parts
make dist            # build all distributable outputs (same as make)
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

In VS Code's **Run and Debug** dropdown, select **Tabcast: daemon +
client**, then press **F5**. VS Code remembers the selected target;
F5 on a standalone Disciple target does not start Apostle.

The compound builds all three parts, then launches Apostle and Disciple
under gdb, with `stopAll` so closing one stops the other. Disciple waits
up to 15 seconds for Apostle's broadcast listener before launching.

| | |
| --- | --- |
| Apostle | `--foreground --log-level debug`, with logs in the integrated terminal. |
| Disciple | No arguments: connects to `127.0.0.1:8421` and opens a window for live frames. |

F5 explicitly builds `BACKEND=test`, which requires
`libgtkmm-3.0-dev`. If it is missing, the build reports the dependency
instead of silently building a client with no display. The separate
**Debug disciple** target still previews the bundled sample image.

Both are debug builds with symbols and undefined behaviour checks.
The VS Code tasks explicitly use `SANITIZERS=undefined`: the development
machine preloads `AppProtection.so` through `/etc/ld.so.preload`, which
conflicts with AddressSanitizer before the programs reach `main()`.
Needs the C/C++ extension (`ms-vscode.cpptools`) and `gdb`.

Normal `make` builds still use `SANITIZERS=address,undefined`. On a
host with the preload conflict, build and test from a terminal with:

```sh
make BUILD=debug BACKEND=test SANITIZERS=undefined
make check SANITIZERS=undefined
```

Sanitizer variants have separate build directories, so switching does
not reuse incompatible objects. On a host compatible with ASan, change
the VS Code tasks to `SANITIZERS=address,undefined` for address checks
too. The launch configurations disable leak detection because it cannot
run under gdb's ptrace. For program errors, read the integrated
**Terminal**; the Debug Console's "exited with code 1" is only the exit
summary.

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

Start **Tabcast: daemon + client** with F5, wait for the Disciple
window, then press **Broadcast** on a normal web page in Firefox.
The extension uploads to `127.0.0.1:8420`; Disciple receives frames on
port `8421`. A "No answer from Apostle" message means the daemon is
not listening at the address configured in the extension.

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
| `build/disciple/<type>/<backend>/` | Client objects and intermediate links, separated by display backend. |
| `build/evangelist/<browser>/` | Unpacked extension. This is what you load with "load unpacked". |
| `bin/apostle`, `bin/disciple` | The finished programs. |
| `bin/evangelist-<version>-firefox.xpi`, `bin/evangelist-<version>-chrome.zip` | Packaged extensions, written by `make` or `make dist`. |

Debug artifacts have an additional sanitizer subdirectory, such as
`build/apostle/debug/undefined/` and
`build/disciple/debug/test/address,undefined/`.

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
