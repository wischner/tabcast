# Apostle

The Tabcast broadcast daemon. The one who is sent to carry the message
to the whole community: it receives frames from the Evangelist browser
extension and distributes them to every registered Disciple, and to the
Linux test client.

Modern C++, Ubuntu Linux.

## Status

**Receives and broadcasts.** Both halves of `docs/PROTOCOL.md` are
served. Evangelist posts a frame over loopback, and it goes straight
out to every registered Disciple.

The process side is complete too: it starts in the foreground or as a
detached daemon, logs to the console or to syslog, takes a locked pid
file, handles signals, and shuts down cleanly, saying bye to the flock
on the way out.

What is not built yet:

- Host-side reduction to `gray-packed`. A frame is forwarded in the
  format the browser encoded it, so the client decodes, scales, and
  quantises. That is the wrong trade for a slow reader and the right
  one for a desktop test client, which is all there is today.
- Pairing. Any host that can reach the broadcast port joins the flock,
  which `docs/PROTOCOL.md` says is acceptable on a trusted LAN and
  nowhere else.

The wire formats for all of it are specified in `docs/PROTOCOL.md` at
the repository root, so the protocol does not have to be invented
while implementing it.

## Ingest

Section 1 of `docs/PROTOCOL.md`, served on `127.0.0.1` only. v1 has no
authentication, so binding anything wider would hand frame injection to
the whole network.

| Endpoint | Answers |
| --- | --- |
| `POST /api/v1/frame` | `200` with `{"ok":true,"sequence":N,"clients":N}`. `400` for a missing or malformed required header, `413` past `maxFrameBytes`, `415` for a body that is not `image/png` or `image/jpeg`. |
| `GET /api/v1/health` | `200` with the version, protocol revision, client count, and frame size limit. |
| `OPTIONS` | `204` with the CORS headers, so curl and devtools work. |

Check it by hand:

```sh
curl -s http://127.0.0.1:8420/api/v1/health

curl -s -X POST http://127.0.0.1:8420/api/v1/frame \
    -H "Content-Type: image/png" \
    -H "X-Tabcast-Sequence: 1" \
    -H "X-Tabcast-Session: manual" \
    -H "X-Tabcast-Width: 1200" \
    -H "X-Tabcast-Height: 720" \
    --data-binary @../disciple/tests/data/sample-page.png
```

Requests are parsed by `include/apostle/http.h`, which has no I/O in it
and is tested from string literals in `tests/test_http.cpp`. Chunked
bodies, pipelining, and folded header lines are refused rather than
half-supported: on an ingest path a half-supported feature is a way to
corrupt a frame quietly.

## Broadcast

Section 2 of `docs/PROTOCOL.md`, on `*:8421`. Unlike ingest this binds
every interface, because the readers are on the LAN and the daemon is
not.

A Disciple connects, sends `hello`, and gets a `welcome` with its
assigned id and the keepalive interval. From then on Apostle pushes
`frame` messages and the Disciple never polls. Each one is answered
with an `ack` carrying how long the panel took to draw it.

**One frame in flight per client.** A frame arriving while the previous
one is unacknowledged replaces it rather than queueing behind it, so a
reader that falls behind shows the newest page and never works through
a backlog of stale ones. A client that never sends an `ack` is paced by
its socket draining instead, because acks are optional in the protocol
and waiting forever for one would stop the stream after a single frame.

Silence for `keepalive` seconds brings a `ping`, and no answer within
another `keepalive` closes the connection.

The envelope is in `include/apostle/wire.h`, which like the HTTP parser
has no I/O in it and is checked byte for byte against the protocol
document in `tests/test_wire.cpp`. That file is duplicated in the
Disciple project, because the two programs ship separately and the
protocol document is the source of truth both copies answer to.

## Build

```sh
make                 # debug build with sanitizers
make BUILD=release   # release build
make test            # build and run the tests
make clean
make help
```

Output goes to the repository root, not into this directory. There is
one `build/` and one `bin/` for all three parts of Tabcast: objects
land in `../build/apostle/<build type>/` and the finished daemon in
`../bin/apostle`.

Needs `g++` with C++20 and GNU Make. Nothing else.

Debug builds carry `-Og -g` and the address and undefined behaviour
sanitizers. Release builds carry `-O2` and link the C++ runtime into
the binary.

On hosts where a system preload such as AppProtection conflicts with
AddressSanitizer, use `make SANITIZERS=undefined` and
`make test SANITIZERS=undefined`. Debug artifacts are separated into
`<build type>/<sanitizers>/` so changing this option forces a fresh
build. The default remains `SANITIZERS=address,undefined`.

## Run

From the repository root:

It runs both ways: attached to a console, or detached as a classic
daemon.

### As a console program

The default. Logs timestamped lines to stderr and stops on Ctrl-C.

```sh
bin/apostle
bin/apostle --log-level debug
```

```
2026-08-21 07:59:44 info  apostle 0.1.0 starting, protocol 1
2026-08-21 07:59:44 info  ingest    http://127.0.0.1:8420/api/v1/frame
2026-08-21 07:59:44 info  broadcast tcp/*:8421
2026-08-21 07:59:44 warn  the listeners are not implemented yet, idling
```

### As a daemon

`--daemon` detaches: double fork, new session, no controlling
terminal, working directory `/`, and stdin, stdout, and stderr all
replaced with `/dev/null`. Because there is no stderr left to write to,
logging switches to syslog automatically.

```sh
bin/apostle --daemon --pid-file /run/apostle.pid
journalctl -t apostle -f
sudo kill -TERM "$(cat /run/apostle.pid)"
```

The pid file is optional and locked, not merely created. A file left
behind by a process that crashed carries no lock, so it does not block
the next start, while a genuinely running instance does:

```
error cannot take pid file /run/apostle.pid: Resource temporarily unavailable
error another apostle may already be running
```

### Under systemd

Use the unit in [scripts/apostle.service](../scripts/apostle.service),
which runs apostle in the **foreground**. That is not a contradiction:
systemd wants to be the direct parent so it can supervise and restart
the process, and it reads stderr straight into the journal. `--daemon`
would hide the process from its supervisor and throw the log away.

```sh
sudo install -m 755 bin/apostle /usr/local/bin/apostle
sudo install -m 644 scripts/apostle.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now apostle
systemctl reload apostle      # sends SIGHUP
journalctl -u apostle -f
```

Reach for `--daemon` only on a machine with no service manager.

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--ingest-port PORT` | 8420 | Loopback port Evangelist posts frames to. |
| `--broadcast-port PORT` | 8421 | Port Disciples connect to. |
| `-d`, `--daemon` | | Detach from the terminal, log to syslog. |
| `-f`, `--foreground` | default | Stay attached, log to stderr. |
| `-p`, `--pid-file PATH` | none | Write and lock a pid file. |
| `-l`, `--log-level LEVEL` | `info` | `error`, `warn`, `info`, or `debug`. |
| `--log WHERE` | follows mode | Force `console` or `syslog`. |
| `--version` | | Print the version and the protocol revision. |
| `-h`, `--help` | | Print the usage text. |

### Signals

| Signal | Effect |
| --- | --- |
| `SIGTERM`, `SIGINT` | Shut down cleanly and remove the pid file. |
| `SIGHUP` | Reload. Nothing to reload yet, so it only logs. |
| `SIGPIPE` | Ignored. A reader vanishing mid-frame must fail one connection, not the daemon. |

Signals are **blocked and collected synchronously** with `sigwait()`,
not handled asynchronously. Nothing runs at an arbitrary point in the
middle of other work, and a signal arriving just before the wait stays
pending rather than being lost. When the listeners arrive this becomes
a `signalfd` in the same poll set.

## Deployment

Apostle ships as one file. A release build links `libstdc++` and
`libgcc` into the binary, so it can be copied onto any Ubuntu machine
without a matching C++ runtime installed:

```sh
make BUILD=release
ldd ../bin/apostle     # linux-vdso, libc, ld-linux. Nothing else.
```

It is deliberately not a fully static `-static` link. glibc resolves
hostnames through NSS modules that it loads at runtime, so a fully
static binary would silently fail to resolve anything.

## Security

The daemon needs no privileges. Both ports are above 1024 and it writes
nothing to disk, so do not run it as root. The systemd unit uses
`DynamicUser=yes` so there is no account to create.

The ingest endpoint must bind loopback only, never `0.0.0.0`. It has
no authentication in protocol v1, so any process on the host can post
a frame. That is acceptable exactly as long as the endpoint is not
reachable from the network.

The broadcast listener is the only part that faces the LAN, and it has
no pairing in v1 either: any host that can reach the port and speak
`hello` joins the flock. Fine on a trusted network, not fine anywhere
else. Pairing tokens are the first thing to add before this leaves a
private network.

## Layout

```
apostle/
├── Makefile
├── include/apostle/     public headers
│   ├── version.h        version, protocol revision, default ports
│   ├── log.h            levels and sinks
│   ├── options.h        the command line
│   └── runtime.h        detaching, pid file, signals
├── src/                 application sources
│   ├── main.cpp         startup order and the wait loop
│   ├── log.cpp
│   ├── options.cpp
│   └── runtime.cpp
├── lib/                 project and third-party libraries
└── tests/
    └── test_options.cpp 23 checks, run with `make test`
```

No `build/` or `bin/` here. Both live at the repository root and are
shared with Evangelist and Disciple. `make distclean` removes
`../build/apostle/` and `../bin/apostle`, and leaves the other parts'
output alone.

Coding rules for this project are in
`docs/standards/CPP-CODING-STYLE.md`.
