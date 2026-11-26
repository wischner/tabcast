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
| E-ink client | **Disciple** | Receives and displays the Word so the faithful can see. Multiple Disciples form the flock. | Modern C++, on [inkerface](../inkerface) |

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
   Disciple  Disciple  Disciple
   XTEINK X4 any       inkerface's
             inkerface X4 emulation
             reader    on Linux
```

## Status

| Part | State |
| --- | --- |
| Evangelist | **Working.** Control bar, capture clock, and frame upload are implemented. |
| Apostle | **Working.** Receives frames over HTTP and broadcasts them to connected readers over TCP. |
| Disciple | **Working on a real Xteink X4.** An inkerface program: on power-up a reader shows a splash, joins Wi-Fi (set up from a phone through its own `sheep_` access point), finds Apostle by itself and shows every page; holding the power button switches it off. The X4 firmware and inkerface's desktop emulation of the X4 are the same code. |

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
sudo apt install build-essential make python3 cmake git patch \
    libx11-dev libssl-dev
git clone https://github.com/wischner/inkerface.git ../inkerface
```

- **Evangelist** needs `python3` and nothing else. No npm, no bundler,
  no network access at build time.
- **Apostle** needs `g++` with C++20 support and GNU Make, nothing else.
- **Disciple** is an [inkerface](https://github.com/wischner/inkerface)
  program, so it needs inkerface checked out beside this repository, as
  `../inkerface`, and what inkerface needs: CMake, X11 and OpenSSL
  headers, and git for its first download of the FreeInk SDK, which its
  own build does. The X4 firmware also needs PlatformIO. See
  `disciple/README.md`.

Firefox or Chrome, to run the extension.

## Debugging with F5

In VS Code's **Run and Debug** dropdown, select **Tabcast: daemon +
client**, then press **F5**. VS Code remembers the selected target;
F5 on a standalone Disciple target does not start Apostle.

The compound builds all three parts, then launches Apostle and Disciple
under gdb, with `stopAll` so closing one stops the other. They can start
in either order: the reader keeps looking for Apostle until one answers.

| | |
| --- | --- |
| Apostle | `--foreground --log-level debug`, with logs in the integrated terminal. |
| Disciple | inkerface's preview window, showing the X4's panel pixel for pixel; any key is a button. The first run goes through Wi-Fi setup, at the address the window shows; its emulated flash in `build/disciple/storage` remembers the result. |

Both are debug builds with symbols and undefined behaviour checks.
The VS Code tasks explicitly use `SANITIZERS=undefined`: the development
machine preloads `AppProtection.so` through `/etc/ld.so.preload`, which
conflicts with AddressSanitizer before the programs reach `main()`.
Needs the C/C++ extension (`ms-vscode.cpptools`) and `gdb`.

Normal `make` builds still use `SANITIZERS=address,undefined`. On a
host with the preload conflict, build and test from a terminal with:

```sh
make BUILD=debug SANITIZERS=undefined
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
appears on the reader, fitted to its portrait panel and reduced to one
bit a pixel. The path is Evangelist captures the tab, posts it to
Apostle over loopback, Apostle reduces it for each reader's panel and
fans it out over TCP, Disciple draws it and acknowledges.

The reader runs on a real Xteink X4: `make x4-upload` in `disciple/`
builds it on inkerface and flashes it, and the same code runs in
inkerface's desktop emulation of the X4. Apostle reduces every page to
each reader's own panel (`image/gray-packed`), so a reader needs no image
decoder. Pages are one bit deep for now; four grey levels need inkerface
to expose the X4's grayscale mode.

## Getting started

```sh
make
```

Then load the extension. Firefox needs one extra step that catches
everyone, so read
[evangelist/README.md](evangelist/README.md#install) rather than
guessing.

Start **Tabcast: daemon + client** with F5, wait for the Disciple
window, set its Wi-Fi up once if it asks (any phone or browser on this
network can open the address it shows), then press **Broadcast** on a
normal web page in Firefox.
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
