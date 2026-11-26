#!/usr/bin/env python3
"""Start Firefox with Evangelist loaded, for `make run`.

Firefox installs an unsigned extension only as a temporary add-on, which
is what about:debugging does by hand. This does the same through
Firefox's remote debugging protocol, as Mozilla's own web-ext does, so
nothing has to be installed. Firefox gets a throwaway profile of its
own, so the browser you use every day is not touched, and it runs
beside it.

Runs until that Firefox is closed. Uses only the Python standard
library.

MIT License (see: LICENSE)
Copyright (C) 2026 Tomaz Stih
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import time


# Preferences that let a script install a temporary add-on: the debugger
# server on, no prompt to allow the connection, and no first-run pages
# in the way.
preferences = {
    "devtools.debugger.remote-enabled": True,
    "devtools.debugger.prompt-connection": False,
    "devtools.chrome.enabled": True,
    "xpinstall.signatures.required": False,
    "browser.shell.checkDefaultBrowser": False,
    "browser.startup.homepage_override.mstone": "ignore",
    "browser.aboutwelcome.enabled": False,
    "datareporting.policy.dataSubmissionEnabled": False,
    "toolkit.telemetry.reportingpolicy.firstRun": False,
    "app.update.enabled": False,
}


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def write_profile(profile):
    profile.mkdir(parents=True, exist_ok=True)
    lines = [f'user_pref("{name}", {json.dumps(value)});\n'
             for name, value in preferences.items()]
    (profile / "user.js").write_text("".join(lines))


class debugger:
    """One connection to Firefox's remote debugging server.

    Packets are the length in decimal, a colon, then that many bytes of
    JSON.
    """

    def __init__(self, port, patience):
        deadline = time.monotonic() + patience
        while True:
            try:
                self.connection = socket.create_connection(
                    ("127.0.0.1", port), timeout=5)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise RuntimeError("Firefox did not open its debugger port")
                time.sleep(0.25)
        self.pending = b""
        self.receive()  # the greeting

    def send(self, packet):
        body = json.dumps(packet).encode()
        self.connection.sendall(str(len(body)).encode() + b":" + body)

    def receive(self):
        while b":" not in self.pending:
            self.pending += self.read()
        length, _, rest = self.pending.partition(b":")
        size = int(length)
        while len(rest) < size:
            rest += self.read()
        self.pending = rest[size:]
        return json.loads(rest[:size])

    def read(self):
        data = self.connection.recv(65536)
        if not data:
            raise RuntimeError("Firefox closed the debugger connection")
        return data

    def request(self, to, kind, **fields):
        """Send one request and return the reply from that actor."""
        self.send({"to": to, "type": kind, **fields})
        while True:
            reply = self.receive()
            if reply.get("from") == to:
                if "error" in reply:
                    raise RuntimeError(
                        f"{kind}: {reply['error']}: {reply.get('message', '')}")
                return reply


def install(link, extension):
    root = link.request("root", "getRoot")
    actor = root.get("addonsActor")
    if not actor:
        raise RuntimeError("this Firefox cannot install temporary add-ons")
    reply = link.request(actor, "installTemporaryAddon",
                         addonPath=str(extension))
    return reply.get("addon", {}).get("id", "evangelist")


def quit_firefox(link):
    """Ask Firefox's parent process to quit.

    A signal is not enough: a snap Firefox is confined, and its processes
    do not always take signals from the script that started them.
    """
    process = link.request("root", "getProcess", id=0)
    target = link.request(process["processDescriptor"]["actor"], "getTarget")
    link.request(target["process"]["consoleActor"], "evaluateJSAsync",
                 text="Services.startup.quit(Ci.nsIAppStartup.eForceQuit)")


def stop(browser, link):
    if browser.poll() is not None:
        return
    if link is not None:
        try:
            quit_firefox(link)
            browser.wait(timeout=10)
            return
        except (OSError, RuntimeError, subprocess.TimeoutExpired):
            pass
    try:
        os.killpg(browser.pid, 15)
        browser.wait(timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("extension", type=Path,
                        help="the unpacked extension, build/evangelist/firefox")
    parser.add_argument("profile", type=Path,
                        help="where to keep the throwaway profile")
    parser.add_argument("--headless", action="store_true",
                        help="no window, for testing this script")
    parser.add_argument("--check", action="store_true",
                        help="install, then close Firefox and exit")
    arguments = parser.parse_args()
    # Stopped by make run when the reader closes: unwind, so Firefox is
    # closed too.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    firefox = shutil.which("firefox")
    if firefox is None:
        print("firefox    not found; load Evangelist by hand from "
              f"{arguments.extension}", file=sys.stderr)
        return 0

    extension = arguments.extension.resolve()
    profile = arguments.profile.resolve()
    write_profile(profile)
    port = free_port()
    command = [firefox, "--profile", str(profile), "--no-remote",
               "--new-instance", "--start-debugger-server", str(port)]
    if arguments.headless:
        command.append("--headless")
    # A session of its own, so stopping it stops all of Firefox's
    # processes and nothing else.
    browser = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL,
                               start_new_session=True)
    link = None
    try:
        link = debugger(port, patience=60)
        name = install(link, extension)
        print(f"firefox    loaded {name} from {extension}", flush=True)
        if arguments.check:
            return 0
        return browser.wait()
    except (OSError, RuntimeError) as error:
        print(f"firefox    could not load Evangelist: {error}; load it by "
              f"hand from about:debugging, {extension}", file=sys.stderr)
        if not extension.is_relative_to(Path.home()):
            print("firefox    a snap Firefox sees only your home folder, "
                  "and this repository is outside it", file=sys.stderr)
        if arguments.check:
            return 1
        return browser.wait()
    except KeyboardInterrupt:
        return 0
    finally:
        stop(browser, link)


if __name__ == "__main__":
    sys.exit(main())
