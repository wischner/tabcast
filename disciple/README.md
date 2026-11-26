# Disciple

The Tabcast e-ink reader. The receiver who contemplates and displays
the Word so the faithful can see and sing: it joins an Apostle daemon,
receives broadcast pages, and shows them on its panel. Multiple
Disciples form the flock.

Disciple is an [inkerface](https://github.com/wischner/inkerface)
program. inkerface is the e-ink platform directly on the FreeInk SDK: it
drives the panel, buttons, Wi-Fi, and storage on the Xteink X4 and on
every other reader it supports, and emulates the X4 on a desktop with
the same FreeInk renderer. Disciple uses inkerface's types directly and
has no platform layer of its own, so the same code is the X4 firmware,
any other inkerface reader, and the desktop emulation.

## Status

**Runs on a real Xteink X4:** Wi-Fi setup from a phone, discovery, and
pages from Apostle have all worked on the device. The same code runs in
inkerface's desktop emulation of the X4.

Switch it on and it behaves the way a reader must, with nobody to tell
it anything:

1. It shows the tabcast splash for three seconds. Pressing a button now
   asks for Wi-Fi setup after it.
2. It gets onto Wi-Fi: it joins the network it has saved, or, the first
   time, becomes an access point a phone can set it up through. See
   [Wi-Fi setup](#wi-fi-setup).
3. It shows **Connecting...** and looks for an Apostle on the LAN by
   broadcasting a `discover` over UDP. No address is configured.
4. It joins the first Apostle that answers. Apostle sends its current
   page straight away; if there is none yet, the reader shows
   **Connected, waiting for the first page**.
5. It shows every page Apostle broadcasts, and acknowledges it. Apostle
   has already reduced the page to this panel, one bit a pixel
   (`image/gray-packed`), so the reader draws it as it arrived: no image
   decoder, no copy.

**Holding the power button** for about a second switches the reader
off: it says goodbye to Apostle, shows **Switched off**, which the
e-paper keeps with no power, and goes into deep sleep. The next press of
the power button boots it afresh, straight back to its network and
Apostle. In the desktop emulation, P is the power button and the program
ends instead.

When the connection drops, or Apostle goes silent for longer than its
keepalive allows, the reader looks for it again by itself, waiting 1, 2,
4, 8, and then 15 seconds between attempts. A page already on the panel
stays there while it does, because a singer halfway through a verse is
better off with the words they had than with a status screen.

What is not done yet:

- Switching off is confirmed on the device; waking again on the power
  button is not yet.
- A considered refresh policy. The reader refreshes fast and does a
  full, flashing refresh every eighth frame to clear ghosting.
- Remembering the last Apostle across power cycles. Discovery is cheap
  enough that a reader simply asks again.

## How it is put together

An inkerface program's main loop waits on the buttons, then does what
is due. Disciple's is exactly that (`src/client.cpp`):

```cpp
inkerface::display screen;
inkerface::input buttons(screen);
inkerface::network net;
inkerface::storage disks;
panel view(screen);
reader disciple(net, disks, view, settings, std::cout);

disciple.start();
while (!buttons.closed()) {
    disciple.step(buttons.poll_for(disciple.wait()));
}
```

`reader::step()` checks every socket with a zero wait, then fires the
reader's one timer if it is due; `wait()` is the time to that timer,
never more than a tenth of a second. Nothing needs a second thread. The
only calls that block are joining a network and connecting to Apostle,
both bounded and both behind **Connecting...** on the panel.

| Module | What it does | On inkerface's |
| --- | --- | --- |
| `reader` | the state machine: splash, Wi-Fi, discovery, join, pages, reconnect, switching off | `network`, `input_snapshot`, `power_off` |
| `wifi_setup` | saved network, or access point, setup page, and phone | `network::scan`, `start_access_point`, `connect` |
| `portal` | the setup page's web server and the captive DNS | `tcp_listener`, `tcp_socket`, `udp_socket` |
| `discovery` | finding Apostle by broadcast | `udp_socket::broadcast` |
| `link` | the protocol connection to Apostle | `tcp_socket` |
| `screens` | splash, status, setup, and pages, with the refresh policy | `display`, `surface`, its fonts |
| `settings` | network, password, and the reader's name | `storage`, `file` on the internal volume |
| `wire`, `setup_page` | the protocol's framing, the setup page's HTML | nothing: pure logic |

The panel's size comes from the display (`frame().extent()`) at run
time, and the reader tells Apostle that size in its hello, so a reader
with a different panel gets pages sized for it. The grey depth is fixed
at two levels: inkerface draws one bit deep on every reader and only
dithers greys, so asking Apostle for more would buy speckle around every
letter.

## Build

`make` at the repository root builds it, with the other two parts, in
the build image; see the top-level README. `PLATFORM` chooses which
Disciple:

```sh
make                        # the emulator, into ../bin/disciple
make PLATFORM=x4            # Xteink X4 firmware, into ../bin/disciple-x4.bin
make deploy PLATFORM=x4     # that firmware, flashed over USB
make run                    # Apostle, Evangelist, and the emulator, here
make help                   # every platform
```

Disciple links inkerface's latest release, which the top-level
`Makefile` keeps in `../deps/inkerface/`; inkerface is never built
here. The emulator links the release's Linux library for the build
type, `linux-release/` or `linux-debug/`, with the flags its pkg-config
file gives, and its `libinkerface_main.a`, whose `main()` calls
Disciple's `program()`. Objects land in `../build/disciple/<build type>/`.

### The firmware

A reader platform is an `[env:...]` in `platformio.ini`, which uses
inkerface's platform library and its FreeInk cache exactly as
inkerface's own firmware does, and builds every source in `src/`.
inkerface's Arduino `setup()` calls `program()`. PlatformIO runs in the
build image, with its toolchains in `../deps/platformio/`. The X4 image
is about 1.4 MB of the 3 MB firmware slot and 47 KB of static RAM.

Another reader inkerface supports is another `[env:...]` with that
reader's board and FreeInk flags, mirroring inkerface's own
`platformio.ini`, and a `custom_description` for `make help`. Disciple's
code does not change, and the new platform is in `make help` at once.

## Run

On a reader it runs with no arguments and needs none. On a desktop,
`make run` at the repository root starts it in the preview window with
its settings kept, beside Apostle and Evangelist. By hand, from the
repository root:

```sh
INKERFACE_PREVIEW=1 bin/disciple --host apostle.lan
INKERFACE_FRAME_PATH=frame.pbm bin/disciple      # headless, each frame to a file
```

`INKERFACE_PREVIEW=1` opens inkerface's window, which shows the X4's
panel pixel for pixel; any key is a button. Without it, input comes from
standard input, one action a line, and the program ends when standard
input does. `INKERFACE_STORAGE_ROOT` is where the emulated internal
flash lives, which is where the reader keeps its Wi-Fi settings and its
name. `make run` and the VS Code launch keep it in
`../build/disciple/storage`; without one, settings last until the
program ends.

```
disciple 0.1.0
  reader   sheep_3b416a2c, panel 480x800 at 2 levels
  apostle  found by discovery, udp port 8421
  setup    join sheep_3b416a2c with password wrmvjax2, then open http://192.168.8.13:8080/
  setup    a phone chose Office
  wifi     joining Office
  wifi     joined Office
  looking  for an apostle on udp port 8421
  found    apostle on choir-laptop at 192.168.8.13:8421
  joined   as c001, keepalive 30s
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--host HOST` | | Join this Apostle instead of looking for one. |
| `--port PORT` | 8421 | Broadcast port: TCP to join, UDP to look on. |
| `--wifi-setup` | | Go straight to Wi-Fi setup, even with a network saved. |
| `--version` | | Print the version and protocol revision. |
| `-h`, `--help` | | Print the usage text. |

Discovery also asks on loopback on a desktop, so the emulation finds an
Apostle on the same machine with no network at all. If a network drops
broadcasts, as some guest Wi-Fi does, use `--host`.

### Wi-Fi setup

A reader has no keyboard, so a phone is its keyboard. With no network
saved, or when a button was pressed during the splash, the reader:

1. Becomes a Wi-Fi access point named after itself: `sheep_` and eight
   random hex digits, such as `sheep_3fa91c07`. The name is chosen on
   first power-on and saved, so it never changes. The network is WPA2,
   with an eight character password made up at power-on and shown on
   the panel, leaving out letters and digits that are easy to confuse.
2. Shows that name and password on its panel, and the address of its
   setup page.
3. Answers every DNS question with its own address and redirects any
   page it does not serve to the setup page. A phone that joins
   therefore sees its usual "sign in to network" prompt and lands on
   the setup page without typing anything.
4. The page lists the networks in range, strongest first, plus "Other
   network" for a hidden one. Pick one, type the password, and press
   **Join network**.
5. The reader leaves its access point and joins that network, showing
   **Connecting... joining _name_**. If that works, the network is
   saved and the reader carries on to find Apostle. If not, it comes
   back as the same access point, with the same password, and both the
   panel and the page say what went wrong.

A network is saved only after it has been joined, so a mistyped
password never replaces one that worked. If the saved network is
missing at power-on, for example because the router is still starting,
the reader falls back to setup. It then retries the saved network every
two minutes for as long as nobody is using the setup page.

Settings hold the network, its password, and the reader's name, in
`disciple.json` on the internal volume. The password is stored as typed,
because the reader has to give it to the network again; anyone who can
read the reader's storage can read it.

On a desktop, inkerface emulates the radio. Its scan lists a fixed set
of networks, joining succeeds at once whatever the password, and the
access point is emulated at this machine's own LAN address, with the
setup page on port 8080 rather than 80. Any phone or browser on the same
network can open that page and go through the whole setup. There is no
captive DNS there, because this machine's network is not the reader's
to answer for.

## Layout

```
disciple/
├── Makefile             the emulator, the firmware, and the tests
├── platformio.ini       the reader platforms, one [env:...] each
├── include/disciple/
│   ├── version.h        version, protocol revision, port, grey levels
│   ├── client.h         the entry logic: platform objects and main loop
│   ├── reader.h         the reader's state machine
│   ├── timer.h          the reader's one deadline
│   ├── wifi_setup.h     saved network, or access point and phone
│   ├── settings.h       what the reader remembers, and its name
│   ├── portal.h         setup web server and captive DNS
│   ├── setup_page.h     the pages the phone sees
│   ├── discovery.h      finding an Apostle over UDP
│   ├── link.h           the TCP connection to Apostle
│   ├── screens.h        everything on the panel
│   ├── options.h        the command line, for desktop runs
│   └── wire.h           the Apostle protocol, shared with apostle/
├── src/                 one file per header, and
│   └── main.cpp         inkerface's program(), nothing else
└── tests/               run in CI, on inkerface's desktop backend
    ├── test_wire.cpp    the envelope, byte for byte
    ├── test_setup.cpp   forms, DNS answers, settings, names, the setup page
    ├── test_screens.cpp screens and pages, pixel for pixel
    ├── test_reader.cpp  discovery, pages, reconnecting, and a phone
    │                    setting a reader up, against stand-ins
    └── data/            a sample page to post to Apostle by hand
```

No `build/` or `bin/` here. Both live at the repository root and are
shared with Evangelist and Apostle.

Coding rules for this project are in
`docs/standards/CPP-CODING-STYLE.md`.
