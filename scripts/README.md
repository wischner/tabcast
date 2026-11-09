# scripts/

Developer automation and toolchains, organised by purpose or target
platform.

- `apostle.service` is the systemd unit for the broadcast daemon. It
  runs apostle in the foreground on purpose, so systemd can supervise
  it and capture its log. See the Apostle README.
- `wait-for-apostle.py` is the live Disciple debug target's startup
  check. It waits up to 15 seconds for the local TCP listener on port
  8421, so the two concurrent debug sessions can start in either order.

Still to come: the cross-compilation toolchain for the XTEINK X4, once
Disciple needs to run on a reader rather than on a development
machine.

Build logic does not belong here. Each part builds through its own
`Makefile`.
