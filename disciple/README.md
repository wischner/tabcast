# Disciple

The Tabcast e-ink client. The receiver who contemplates and displays
the Word so the faithful can see and sing: it joins an Apostle daemon,
receives broadcast frames, and renders them on the panel. Multiple
Disciples form the flock.

Modern C++, FreeInk SDK. The first target device is the XTEINK X4.

## Status

**The client works. The device does not exist yet.**

Run it with no arguments and it joins an Apostle, receives broadcast
frames, and shows each one in a window: decode, fit to 480 x 800,
reduce to four grey levels, display, acknowledge. `--replay FILE` runs
a local image through the same pipeline without a daemon, which is how
you check a page renders before there is anything broadcasting.

What is not built yet:

- The FreeInk render path and the panel refresh policy, so there is no
  Disciple that runs on a reader. `BACKEND=test` is the only one that
  draws anything.
- `image/gray-packed`. The client says it accepts PNG and JPEG and
  does the decoding itself. Letting Apostle reduce on the host is the
  right trade for a slow reader and pointless for a desktop window.

Streaming needed one thing the first design did not have. GTK owns the
event loop once `Gtk::Application::run` starts, so a client cannot
block on a socket and on a window at once. `backend::watch()` hands the
socket to the toolkit's own loop, which waits on both with no second
thread and no polling.

## Target panel

| | |
| --- | --- |
| Resolution | 480 x 800, portrait |
| Grey levels | 4, black and white included |
| Bits per pixel | 2, derived from the level count |
| Packed frame | 96000 bytes |

Grey depth is counted in **levels**, not bits, everywhere in this
project. Four levels is two bits per pixel. Calling it "4 bit grey"
would mean sixteen levels, four times the data, and a picture the
panel cannot show. `panel_gray_bits()` in
`include/disciple/version.h` derives the bit count from the level
count so the two cannot drift apart.

Everything downstream sizes itself from these numbers.

## Build

```sh
make                 # debug
make BUILD=release   # release build
make BACKEND=none    # no dependencies at all
make test
make clean
make help
```

Needs `g++` with C++20 and GNU Make, plus gtkmm for the test client.
Output goes to the repository root, not into this directory: objects
land in `../build/disciple/<build type>/` and the finished client in
`../bin/disciple`.

### Backends

`BACKEND` decides what the client can draw on. It is a compile time
choice, so exactly one backend is ever compiled and an uninstalled
dependency cannot break a build that does not use it.

| `BACKEND` | What it does | Needs |
| --- | --- | --- |
| `test` | Opens a window on this machine. **This is the test client.** | gtkmm 3 |
| `none` | No display, no decoder. The protocol and the panel model still build and test. | nothing |
| `freeink` | The real panel on the device. Not written yet. | FreeInk SDK |

The default is `test` when `pkg-config` finds `gtkmm-3.0` and `none`
when it does not, so a fresh checkout always builds while never
silently skipping a display it could have had.

### Building the test client

```sh
sudo apt install libgtkmm-3.0-dev    # once
make                                 # now picks BACKEND=test
```

gtkmm is the C++ binding for GTK 3. It was chosen over a C toolkit
because this project is C++ everywhere else, and over a bare GTK 3 C
API because `Glib::RefPtr` matches the ownership rules the rest of the
client already follows. It brings gdk-pixbuf with it, which is where
the PNG and JPEG decoding comes from, so the client needs no image
library of its own.

It is found through `pkg-config` like any other system library, so
`make GTKMM=<module>` is there if your distribution calls the module
something other than `gtkmm-3.0`.

Unlike the rest of the dependencies, gtkmm links dynamically. That is
fine for a tool that only ever runs on a development machine; the
device build is `BACKEND=freeink` and does not link it at all.

### Building for the device

`BACKEND=freeink` links the SDK. Point `FREEINK_DIR` at an unpacked
copy:

```sh
make BACKEND=freeink FREEINK_DIR=/opt/freeink
```

The SDK is linked statically, as `-l:libfreeink.a`. If the vendor ever
ships it as a shared object only, that fact belongs in this section
rather than being quietly worked around.

## Run

From the repository root:

```sh
bin/disciple                                # join the local apostle
bin/disciple --host apostle.lan --zoom 2    # join one on the network
bin/disciple --replay page.png              # no daemon, one local file
bin/disciple --replay page.png --save out.png --no-view
```

With no `--replay` it connects, sends `hello`, and opens a window on
blank white paper. The window fills the first time Apostle broadcasts
a frame, and closing it leaves the flock.

```
disciple 0.1.0
  device   XTEINK-X4 480x800, 4 grey levels (2 bpp, 96000 bytes a frame)
  backend  test (gtkmm window on this machine)
  source   page.png, 1200x720
  rendered 480x800, packs to 96000 bytes
```

| Option | Default | Meaning |
| --- | --- | --- |
| `-r`, `--replay FILE` | | Render a local PNG or JPEG instead of connecting. A sample page lives in `tests/data/`. |
| `-s`, `--save FILE` | | Write the rendered panel frame to a PNG. |
| `-z`, `--zoom N` | 1 | Magnify the viewer, 1 to 8. Repeats pixels, never smooths. |
| `-n`, `--no-view` | | Render without opening a window. |
| `--host HOST` | 127.0.0.1 | Apostle to join. |
| `--port PORT` | 8421 | Broadcast port. |
| `--version` | | Print the version, protocol revision, and backend. |
| `-h`, `--help` | | Print the usage text. |

The version line names the backend, so it is never a mystery whether a
given binary can actually draw:

```
disciple 0.1.0 (protocol 1, test (gtkmm window on this machine))
```

Use `make run` rather than launching the debug binary by hand. GTK
allocates a good deal at startup and never frees it, and a debug build
links the address sanitizer, so a windowed run would otherwise end in a
page of leak reports and a non-zero exit. `make run` points
LeakSanitizer at `scripts/lsan.supp`, which silences the toolkit's
allocations by library name and leaves a leak in this client's own code
still reported. The VS Code launch configuration does the same.

### What the preview is for

The viewer shows the frame with **no smoothing and no dithering**, at
the panel's real size and depth. That is the point: it is a truth
teller, not a flattering preview. `--zoom` repeats pixels rather than
interpolating them, so magnifying never hides a quantisation edge.

Dithering is deliberately not used. It wins on photographs and loses
badly on text, turning solid black letters into grey speckle. This
pipeline carries lyric and hymn sheets, so text wins.

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
├── include/disciple/
│   ├── version.h        panel geometry, grey levels, ports
│   ├── panel.h          the panel model: fit, quantise, pack
│   ├── backend.h        what a build can decode and display
│   ├── wire.h           the Apostle protocol, shared with apostle/
│   ├── net.h            joining a daemon and reading frames
│   ├── options.h        the command line
│   └── client.h         the entry logic, callable without a main()
├── src/
│   ├── main.cpp         nothing but main()
│   ├── client.cpp       argument handling, replay, and streaming
│   ├── net.cpp          the connection to Apostle
│   ├── wire.cpp         the framed protocol, no I/O in it
│   ├── panel.cpp        no dependencies, fully tested
│   ├── options.cpp
│   ├── backend_none.cpp     BACKEND=none
│   └── backend_test.cpp     BACKEND=test, gtkmm
├── lib/                 project and third-party libraries
├── scripts/
│   └── lsan.supp        leak reports GTK owns, not this client
└── tests/
    ├── test_panel.cpp   run with `make test`
    ├── test_wire.cpp    the envelope, byte for byte
    └── test_options.cpp
```

`panel.cpp` depends on nothing at all: no image library, no display, no
sockets. It takes RGBA in and produces the exact bytes the panel shows,
which is why it can be tested without a window or a device.

No `build/` or `bin/` here. Both live at the repository root and are
shared with Evangelist and Apostle. `make distclean` removes
`../build/disciple/` and `../bin/disciple`, and leaves the other parts'
output alone.

Coding rules for this project are in
`docs/standards/CPP-CODING-STYLE.md`.
