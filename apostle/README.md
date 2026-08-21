# Apostle

The Tabcast broadcast daemon. The one who is sent to carry the message
to the whole community: it receives frames from the Evangelist browser
extension and distributes them to every registered Disciple, and to the
Linux test client.

Modern C++, Ubuntu Linux.

## Status

**Skeleton.** It parses its command line, reports what it would listen
on, and exits with a failure status saying the listeners are not
implemented yet. It does not serve.

What is not built yet:

- The loopback HTTP ingest endpoint, `POST /api/v1/frame`.
- The health endpoint, `GET /api/v1/health`.
- The TCP broadcast listener and the Disciple registry.
- The current-frame store and the per-client fan-out.

The wire formats for all of it are specified in `docs/PROTOCOL.md` at
the repository root, so the protocol does not have to be invented
while implementing it.

## Build

```sh
make                 # debug build with sanitizers, into bin/apostle
make BUILD=release   # release build, into bin/apostle
make test            # build and run the tests
make clean
make help
```

Needs `g++` with C++20 and GNU Make. Nothing else.

Debug builds carry `-Og -g` and the address and undefined behaviour
sanitizers. Release builds carry `-O2` and link the C++ runtime into
the binary.

## Run

```sh
bin/apostle --help
bin/apostle --ingest-port 8420 --broadcast-port 8421
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--ingest-port PORT` | 8420 | Loopback port Evangelist posts frames to. |
| `--broadcast-port PORT` | 8421 | Port Disciples connect to. |
| `--version` | | Print the version and the protocol revision. |
| `--help` | | Print the usage text. |

## Deployment

Apostle ships as one file. A release build links `libstdc++` and
`libgcc` into the binary, so it can be copied onto any Ubuntu machine
without a matching C++ runtime installed:

```sh
make BUILD=release
ldd bin/apostle        # linux-vdso, libc, ld-linux. Nothing else.
```

It is deliberately not a fully static `-static` link. glibc resolves
hostnames through NSS modules that it loads at runtime, so a fully
static binary would silently fail to resolve anything.

## Security

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
