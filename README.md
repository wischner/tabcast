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
| Apostle | **Skeleton.** Builds and runs, does not yet serve. |
| Disciple | **Skeleton.** Builds and runs, no FreeInk SDK wired up. |

The wire protocol for both hops is specified in full in
[docs/PROTOCOL.md](docs/PROTOCOL.md), so neither daemon has to invent
it while being written.

## Build

Everything, from the repository root:

```sh
make                 # build all three parts
make check           # syntax check the extension, run the C++ tests
make BUILD=release   # release build of the C++ parts
make dist            # package the extension for side-loading
make clean
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
sudo apt install build-essential make python3
```

- **Evangelist** needs `python3` and nothing else. No npm, no bundler,
  no network access at build time.
- **Apostle** and **Disciple** need `g++` with C++20 support and GNU
  Make. No cmake.
- **Disciple** additionally needs the FreeInk SDK to drive a real
  panel. Without it, it builds headless, which is enough to exercise
  the whole pipeline from a development machine.

Firefox or Chrome, to run the extension.

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
└── scripts/              developer automation
```

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
