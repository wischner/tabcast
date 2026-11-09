#!/usr/bin/env python3
"""Wait for the local broadcast listener before VS Code starts Disciple.

Compound debug sessions start concurrently. Disciple connects only once,
so wait here until Apostle has bound its socket, with a bounded timeout
and a visible error if the daemon fails to start.

MIT License (see: LICENSE)
Copyright (C) 2026 Tomaz Stih
"""

import socket
import sys
import time


def main():
    address = ("127.0.0.1", 8421)
    deadline = time.monotonic() + 15
    print("Waiting for Apostle at 127.0.0.1:8421...", flush=True)
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(address, timeout=0.5):
                print("Apostle is listening; starting Disciple.", flush=True)
                return 0
        except OSError:
            time.sleep(0.1)

    print(
        "Apostle did not listen on 127.0.0.1:8421 within 15 seconds. "
        "Select 'Tabcast: daemon + client' in Run and Debug, and check "
        "Apostle's terminal for startup errors.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
