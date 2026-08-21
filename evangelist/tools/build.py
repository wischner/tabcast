#!/usr/bin/env python3
#
# Evangelist build tool.
#
# Build tool, run from the project Makefile. Produces one loadable,
# unpacked extension tree per browser under build/, and optionally
# packages each into an archive.
#
# There is one source tree and one manifest. Per-browser differences,
# which amount to the background entry point and the Firefox add-on id,
# live in manifest.<browser>.json and are merged over the base here.
# Nothing in the JavaScript branches on the browser.
#
# Standard library only, on purpose. The extension has no package
# manager and no node_modules, so a clean checkout builds offline.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

import argparse
import json
import os
import shutil
import sys
import zipfile

# Browsers we build for, and the archive extension each one wants.
# Firefox takes an .xpi, which is a zip under a different name.
TARGETS = {"firefox": ".xpi", "chrome": ".zip"}

# Copied into every build. Anything not listed here stays out of the
# shipped extension.
SOURCE_DIRS = ("common", "background", "content", "options", "icons")

# Never copied, even from inside a source directory.
EXCLUDED_SUFFIXES = (".orig", ".rej", ".swp", "~")


def load_manifest(src_dir, target):
    """Merge the base manifest with one browser's overrides.

    Parameters:
        src_dir - Extension source directory.
        target  - Browser name, a key of TARGETS.

    Returns:
        The merged manifest as a dictionary.

    Notes:
        The merge is deliberately shallow. A deep merge would let an
        override half-replace a nested object, which is exactly the
        kind of surprise a two-file manifest is meant to avoid.
    """
    with open(os.path.join(src_dir, "manifest.json")) as handle:
        manifest = json.load(handle)

    override_path = os.path.join(src_dir, "manifest.%s.json" % target)
    with open(override_path) as handle:
        manifest.update(json.load(handle))

    return manifest


def copy_sources(src_dir, out_dir):
    """Copy the shipping source tree into a clean output directory."""
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    def ignored(_directory, names):
        return [
            name for name in names
            if name.endswith(EXCLUDED_SUFFIXES)
        ]

    for name in SOURCE_DIRS:
        source = os.path.join(src_dir, name)
        if os.path.isdir(source):
            shutil.copytree(source, os.path.join(out_dir, name),
                            ignore=ignored)


def manifest_references(manifest):
    """Collect every file path a manifest points at.

    Returns:
        A sorted list of extension-relative paths.

    Notes:
        Used to fail the build on a typo rather than let the browser
        report it hours later as a blank options page.
    """
    paths = set()

    for size_map in (manifest.get("icons"),
                     manifest.get("action", {}).get("default_icon")):
        if isinstance(size_map, dict):
            paths.update(size_map.values())

    background = manifest.get("background", {})
    if "service_worker" in background:
        paths.add(background["service_worker"])
    paths.update(background.get("scripts", []))

    options_page = manifest.get("options_ui", {}).get("page")
    if options_page:
        paths.add(options_page)

    for entry in manifest.get("content_scripts", []):
        paths.update(entry.get("js", []))
        paths.update(entry.get("css", []))

    for entry in manifest.get("web_accessible_resources", []):
        paths.update(entry.get("resources", []))

    return sorted(paths)


def verify(out_dir, manifest):
    """Check that every path the manifest names really exists.

    Returns:
        A list of missing paths, empty when the build is sound.
    """
    return [
        path for path in manifest_references(manifest)
        if not os.path.exists(os.path.join(out_dir, path))
    ]


def package(out_dir, archive_path):
    """Zip an unpacked extension tree into a loadable archive.

    Notes:
        Entries are added in sorted order and the archive is written
        deflated, so two builds of the same sources produce the same
        file listing.
    """
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for directory, subdirs, files in os.walk(out_dir):
            subdirs.sort()
            for name in sorted(files):
                full = os.path.join(directory, name)
                archive.write(full, os.path.relpath(full, out_dir))


def build(src_dir, build_dir, target, make_archive):
    """Build one browser target.

    Returns:
        0 on success, 1 when the built tree is missing a file its
        manifest references.
    """
    manifest = load_manifest(src_dir, target)
    out_dir = os.path.join(build_dir, target)

    copy_sources(src_dir, out_dir)
    with open(os.path.join(out_dir, "manifest.json"), "w") as handle:
        json.dump(manifest, handle, indent=4)
        handle.write("\n")

    missing = verify(out_dir, manifest)
    if missing:
        print("error: %s manifest references missing files:" % target,
              file=sys.stderr)
        for path in missing:
            print("    %s" % path, file=sys.stderr)
        return 1

    print("built %s" % out_dir)

    if make_archive:
        name = "evangelist-%s-%s%s" % (
            manifest["version"], target, TARGETS[target]
        )
        archive_path = os.path.join(build_dir, name)
        package(out_dir, archive_path)
        print("packaged %s" % archive_path)

    return 0


def main(argv):
    """Parse arguments and build every requested target."""
    parser = argparse.ArgumentParser(description="Build Evangelist.")
    parser.add_argument("--src", default="src",
                        help="extension source directory")
    parser.add_argument("--out", default="build",
                        help="build output directory")
    parser.add_argument("--target", action="append", choices=TARGETS,
                        help="browser to build, repeatable, "
                             "defaults to all of them")
    parser.add_argument("--zip", action="store_true",
                        help="also package each target into an archive")
    args = parser.parse_args(argv[1:])

    os.makedirs(args.out, exist_ok=True)

    for target in args.target or sorted(TARGETS):
        status = build(args.src, args.out, target, args.zip)
        if status != 0:
            return status
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
