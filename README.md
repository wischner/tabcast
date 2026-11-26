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
| E-ink client | **Disciple** | Receives and displays the Word so the faithful can see. Multiple Disciples form the flock. | Modern C++, on [inkerface](https://github.com/wischner/inkerface) |

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

## Build, run, deploy

Everything is the top-level `Makefile`, and it has six commands:

```sh
make                      # build Evangelist, Apostle, and Disciple into bin/
make run                  # build, then run all three on this machine
make deploy PLATFORM=x4   # build Disciple for a reader and flash it over USB
make clean                # delete everything builds made; keep deps/
make purge                # clean, and delete deps/ and the build image
make help                 # the commands, and every platform
```

`PLATFORM` says which Disciple to build, run, or deploy. `emulator`, the
default, is the reader in a window on this machine. Every other platform
is a reader, one `[env:...]` in
[disciple/platformio.ini](disciple/platformio.ini); `make help` lists
them. Today that is `x4`, the Xteink X4. `make PLATFORM=x4` builds its
firmware without flashing it.

### What a machine needs

Make, git, curl, and Docker. Nothing else is installed on the machine.
Every build runs in a Docker image, described by
[docker/Dockerfile](docker/Dockerfile), with the compilers, CMake,
PlatformIO, and the headers in it. `make` builds that image the first
time, then runs the build in it with this repository mounted at its own
path and as you, so what it writes is yours and lands where it would
without Docker. Editing the Dockerfile makes the next `make` build a
new image. `make DOCKER=no` builds with this machine's own tools
instead, if they are all there.

Third-party libraries are downloaded the first time a build needs them,
into `deps/`, and kept:

| Library | For | Pinned in the `Makefile` |
| --- | --- | --- |
| [inkerface](https://github.com/wischner/inkerface) | Disciple's platform. It brings the FreeInk SDK, which it downloads into itself. | `INKERFACE_REV` |
| [stb_image](https://github.com/nothings/stb) | Apostle's PNG and JPEG decoder, checked against its SHA-256. | `STB_REV`, `STB_SHA256` |
| PlatformIO toolchains | Reader firmware. | by `disciple/platformio.ini` |

A library already in `deps/` is left alone. When a pin moves, a
downloaded inkerface follows it, unless it has local changes. Downloads
use this machine's git, so the inkerface repository needs only to be
readable by you. To build against an inkerface checkout of your own,
`make INKERFACE_DIR=../inkerface`; that one is never downloaded, moved,
or cleaned.

### Running it

`make run` builds, then starts:

| | |
| --- | --- |
| Apostle | in the foreground, logging to the terminal. If one is already running, that one is used. |
| Evangelist | in a Firefox of its own, with a throwaway profile in `build/firefox/`, loaded as a temporary add-on, so your own Firefox is not touched. Without Firefox, it says where to load the extension from by hand. |
| Disciple | inkerface's preview window, showing the X4's panel pixel for pixel; any key is a button and P is the power button. The first run goes through Wi-Fi setup, at the address the window shows; the emulated flash in `build/disciple/storage/` remembers the result. |

Press **Broadcast** on any page in that Firefox and the page appears in
the reader's window. Closing the reader, or switching it off with P,
stops everything; so does Ctrl+C.

`make run PLATFORM=x4` runs Apostle and Evangelist the same way, and the
reader is the X4 itself, deployed beforehand: switch it on and it finds
this Apostle.

### Deploying to a reader

Connect the reader by USB and switch it on, then:

```sh
make deploy PLATFORM=x4
```

That builds the firmware and flashes it to the first reader on USB.
`PORT=/dev/ttyACM1` names another. The reader keeps its Wi-Fi settings
and its name across a deploy.

### Output

There is one `build/` and one `bin/`, both here at the root. No part
writes anything beside its own sources, so `git status` on a fully built
tree is clean, and `make clean` leaves only what git tracks, plus
`deps/`.

| | |
| --- | --- |
| `bin/apostle` | The daemon. |
| `bin/disciple` | The reader, emulated on this machine. |
| `bin/disciple-<platform>.bin` | The reader's firmware, such as `bin/disciple-x4.bin`. |
| `bin/evangelist-<version>-firefox.xpi`, `-chrome.zip` | The extension, packaged. |
| `build/<part>/` | Everything intermediate. `build/evangelist/<browser>/` is the unpacked extension, to load by hand with "load unpacked". |
| `deps/` | Downloaded libraries and toolchains. Not checked in. |

Builds are release builds, which is what `bin/` is for. `BUILD=debug`
adds symbols and sanitizers, which is what F5 uses.

## Debugging with F5

In VS Code's **Run and Debug** dropdown, select **Tabcast: daemon +
client**, then press **F5**. It runs `make BUILD=debug
SANITIZERS=undefined`, in the build image like every build, then
launches Apostle and Disciple under gdb on this machine, with `stopAll`
so closing one stops the other. They can start in either order: the
reader keeps looking for Apostle until one answers. The image mounts the
repository at its own path, so the debug information points at these
sources.

The sanitizers are UBSan only: the development machine preloads
`AppProtection.so` through `/etc/ld.so.preload`, which conflicts with
AddressSanitizer before the programs reach `main()`. Needs the C/C++
extension (`ms-vscode.cpptools`) and `gdb`. For program errors, read
the integrated **Terminal**; the Debug Console's "exited with code 1" is
only the exit summary.

## How it works

Press Broadcast in the browser and the page appears on the reader,
fitted to its portrait panel and reduced to one bit a pixel. Evangelist
captures the tab and posts it to Apostle over loopback, on
`127.0.0.1:8420`. Apostle reduces it for each reader's panel
(`image/gray-packed`), so a reader needs no image decoder, and fans it
out over TCP on port `8421`. Disciple draws it and acknowledges. A "No
answer from Apostle" message in the browser means the daemon is not
listening at the address configured in the extension.

Pages are one bit deep for now; four grey levels need inkerface to
expose the X4's grayscale mode.

## Repository layout

```
tabcast/
├── Makefile              the only one you need
├── docker/Dockerfile     the build image
├── docs/
│   ├── ARCHITECTURE.md   how the parts fit together, and why
│   ├── PROTOCOL.md       both wire formats, in full
│   └── standards/        coding standards, binding on all agents
│       ├── CPP-CODING-STYLE.md
│       └── EXT-CODING-STYLE.md
├── evangelist/           browser extension
├── apostle/              broadcast daemon
├── disciple/             e-ink reader
├── packaging/            the Debian package
├── scripts/              what make runs: downloads, make run, Firefox
├── build/                every artifact, from every part   (not checked in)
├── bin/                  every finished program            (not checked in)
└── deps/                 downloaded libraries              (not checked in)
```

Each part has its own `Makefile`, which the top-level one runs; use the
top-level one.

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
