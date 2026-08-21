# Disciple

The Tabcast e-ink client. The receiver who contemplates and displays
the Word so the faithful can see and sing: it joins an Apostle daemon,
receives broadcast frames, and renders them on the panel. Multiple
Disciples form the flock.

Modern C++, FreeInk SDK. The first target device is the XTEINK X4.

## Status

**Skeleton.** It parses its command line, reports which Apostle it
would join and what it would tell the daemon about this panel, then
exits with a failure status saying the client is not implemented yet.
Nothing is drawn.

What is not built yet:

- The TCP connection to Apostle and the `hello`/`welcome` handshake.
- The framed message reader from `docs/PROTOCOL.md`.
- PNG decoding and grey level reduction.
- The FreeInk render path and panel refresh policy.

Two things must be confirmed against real hardware before any of that
is worth writing. Both are marked `PLACEHOLDER` in
`include/disciple/version.h`:

- The panel geometry. `1404x1872` has not been read off an XTEINK X4.
- The grey depth. 4 bits per pixel is a guess.

Everything downstream sizes itself from those numbers, so they are not
a cosmetic detail.

## Build

```sh
make                 # debug, headless, into bin/disciple
make BUILD=release   # release build
make HEADLESS=0      # build against the FreeInk SDK
make test
make clean
make help
```

Needs `g++` with C++20 and GNU Make.

`HEADLESS=1` is the default and builds without the FreeInk SDK. The
client still speaks the whole protocol, it just has nowhere to draw.
This is how the Linux test client is built, and it is what lets the
whole pipeline be exercised without a reader on the desk.

`HEADLESS=0` links the SDK. Point `FREEINK_DIR` at an unpacked copy:

```sh
make HEADLESS=0 FREEINK_DIR=/opt/freeink
```

The SDK is linked statically, as `-l:libfreeink.a`. If the vendor ever
ships it as a shared object only, that fact belongs in this section
rather than being quietly worked around.

## Run

```sh
bin/disciple --help
bin/disciple --host 192.168.1.10 --port 8421
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--host HOST` | 127.0.0.1 | Apostle to join. |
| `--port PORT` | 8421 | Broadcast port. |
| `--version` | | Print the version, protocol revision, and back end. |
| `--help` | | Print the usage text. |

The version line names the back end, so it is never a mystery whether
a given binary can actually drive a panel:

```
disciple 0.1.0 (protocol 1, headless (no FreeInk SDK))
```

## Deployment

Disciple ships as one file. A release build links `libstdc++` and
`libgcc` into the binary, so it can be copied onto a reader with
nothing to install beside it. It is not a fully static `-static` link,
for the glibc NSS reason described in
`docs/standards/CPP-CODING-STYLE.md`.

Cross-compiling for the reader is not set up yet. When it is, the
toolchain belongs in `scripts/`.

## Layout

```
disciple/
├── Makefile
├── include/disciple/    public headers
│   └── version.h
├── src/                 application sources
│   └── main.cpp
├── lib/                 project and third-party libraries
├── tests/               automated tests
├── build/               all build artifacts, per build type
└── bin/                 the finished program
```

Coding rules for this project are in
`docs/standards/CPP-CODING-STYLE.md`.
