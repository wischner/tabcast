#!/usr/bin/env python3
"""Keep deps/inkerface at inkerface's latest release.

Tabcast does not build inkerface. It links the libraries an inkerface
release publishes, and always the latest one: every build asks GitHub
which release is latest, and downloads it only when it is not the one
already in deps/inkerface. A build that cannot ask, because it is
offline say, carries on with the release it has.

A release has one archive per platform. They are unpacked side by side:

    deps/inkerface/
      VERSION          the release tag, such as v0.0.1
      linux-release/   include/, lib/ with pkgconfig/inkerface.pc
      linux-debug/     the same, built for debugging
      x4/              include/, lib/libinkerface.a, platformio.ini,
                       partitions-x4.csv

The X4's platformio.ini is the release's own, with its [inkerface] dir
pointed at x4/, so Disciple's firmware builds with exactly the platform
and libraries the release was built with.

inkerface's repository may be private. Then GitHub answers only with a
token that can read it, taken from GITHUB_TOKEN or GH_TOKEN.

Everything is downloaded and unpacked beside deps/inkerface first and
swapped in only once complete, so an interrupted download never leaves a
half release that looks whole. Standard library only.

MIT License (see: LICENSE)
Copyright (C) 2026 Tomaz Stih
"""

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import tarfile
import urllib.error
import urllib.request


repository = "wischner/inkerface"
api = f"https://api.github.com/repos/{repository}"

# The release archives Tabcast uses, by where they are unpacked. The
# names are inkerface's: inkerface-<version>-<platform>.tar.gz.
def archives(version):
    machine = platform.machine()
    return {
        "linux-release": f"inkerface-{version}-linux-{machine}.tar.gz",
        "linux-debug": f"inkerface-{version}-linux-{machine}-debug.tar.gz",
        "x4": f"inkerface-{version}-x4.tar.gz",
    }


def token():
    return os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")


class keep_redirect(urllib.request.HTTPRedirectHandler):
    """Hand a redirect back instead of following it.

    GitHub sends an asset to its storage with a redirect, and that
    storage refuses a request that still carries GitHub's token.
    """

    def redirect_request(self, *arguments):
        return None


def request(url, accept, authorised=True):
    headers = {"Accept": accept, "User-Agent": "tabcast-build",
               "X-GitHub-Api-Version": "2022-11-28"}
    if authorised and token():
        headers["Authorization"] = f"Bearer {token()}"
    return urllib.request.Request(url, headers=headers)


def latest_release(base):
    with urllib.request.urlopen(
            request(f"{base}/releases/latest", "application/vnd.github+json"),
            timeout=30) as reply:
        return json.load(reply)


def download(asset, destination):
    """Fetch one release asset to a file."""
    opener = urllib.request.build_opener(keep_redirect)
    try:
        reply = opener.open(request(asset["url"], "application/octet-stream"),
                            timeout=60)
    except urllib.error.HTTPError as redirect:
        if redirect.code not in (301, 302, 303, 307, 308):
            raise
        reply = urllib.request.urlopen(
            request(redirect.headers["Location"], "application/octet-stream",
                    authorised=False), timeout=300)
    with reply, open(destination, "wb") as out:
        shutil.copyfileobj(reply, out)


def unpack(archive, destination):
    """Unpack an archive whose contents sit in one top-level directory."""
    with tarfile.open(archive) as tar:
        members = []
        for member in tar.getmembers():
            parts = Path(member.name).parts
            if len(parts) < 2:
                continue
            member.name = str(Path(*parts[1:]))
            members.append(member)
        tar.extractall(destination, members=members, filter="data")


def point_x4_at(staged, final):
    """Point the X4 release's platformio.ini at where it will be."""
    ini = staged / "platformio.ini"
    lines = ini.read_text().splitlines(keepends=True)
    section = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("["):
            section = stripped
        elif section == "[inkerface]" and stripped.startswith("dir"):
            lines[index] = f"dir = {final}\n"
    ini.write_text("".join(lines))


def install(release, target):
    tag = release["tag_name"]
    version = tag.removeprefix("v")
    published = {asset["name"]: asset for asset in release.get("assets", [])}
    staging = target.with_name(target.name + ".download")
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    try:
        for folder, name in archives(version).items():
            if name not in published:
                raise RuntimeError(f"release {tag} has no {name}")
            print(f"deps       downloading {name}", flush=True)
            archive = staging / name
            download(published[name], archive)
            unpack(archive, staging / folder)
            archive.unlink()
        (staging / "VERSION").write_text(tag + "\n")
        # Paths written into the release must be where it ends up.
        point_x4_at(staging / "x4", target / "x4")
        old = target.with_name(target.name + ".old")
        shutil.rmtree(old, ignore_errors=True)
        if target.exists():
            target.rename(old)
        staging.rename(target)
        shutil.rmtree(old, ignore_errors=True)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("target", type=Path, help="deps/inkerface")
    parser.add_argument("--api", default=api, help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    target = arguments.target.resolve()

    have = None
    if (target / "VERSION").is_file():
        have = (target / "VERSION").read_text().strip()

    try:
        release = latest_release(arguments.api)
    except (OSError, ValueError) as error:
        reason = getattr(error, "reason", error)
        if isinstance(error, urllib.error.HTTPError) and error.code == 404:
            reason = ("no release found; inkerface's repository is private, "
                      "so set GITHUB_TOKEN to a token that can read "
                      f"{repository}, or make it public")
        if have:
            print(f"deps       cannot ask for inkerface's latest release "
                  f"({reason}); using {have}", file=sys.stderr)
            return 0
        print(f"deps       cannot download inkerface: {reason}",
              file=sys.stderr)
        return 1

    tag = release["tag_name"]
    if tag == have:
        return 0
    print(f"deps       inkerface {tag}"
          + (f", replacing {have}" if have else ""), flush=True)
    try:
        install(release, target)
    except (OSError, RuntimeError, tarfile.TarError) as error:
        print(f"deps       inkerface {tag} failed to install: {error}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
