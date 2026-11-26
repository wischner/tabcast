# scripts/

Developer automation and toolchains, organised by purpose or target
platform.

- `apostle.service` is the systemd unit for the broadcast daemon. It
  runs apostle in the foreground on purpose, so systemd can supervise
  it and capture its log. See the Apostle README.

The reader's firmware is built by PlatformIO from
`disciple/platformio.ini`, on inkerface; there is no toolchain to keep
here.

Build logic does not belong here. Each part builds through its own
`Makefile`.
