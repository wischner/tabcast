#!/usr/bin/env bash
#
# Fetch a pinned third-party file into deps/, if it is not there already.
#
# The pin, a URL naming an exact revision, and the file's SHA-256 live in
# the top-level Makefile. A file already present is left alone, so this
# costs nothing on every build after the first. inkerface is not fetched
# here: it is a release, kept current by scripts/inkerface.py.
#
#   deps.sh file <path> <url> <sha256>
#
# Downloads go to a temporary name beside the destination and are
# renamed into place only once complete, so an interrupted download
# never leaves a half file that looks whole.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

set -euo pipefail

fetch_file() {
    local path="$1" url="$2" sha256="$3"

    if [[ -f "$path" ]]; then
        return 0
    fi

    echo "deps       downloading $(basename "$path") from $url"
    mkdir -p "$(dirname "$path")"
    local staging="$path.download"
    curl --fail --silent --show-error --location --output "$staging" "$url"
    if ! echo "$sha256  $staging" | sha256sum --check --status; then
        rm -f "$staging"
        echo "deps       $(basename "$path") does not match its checksum" >&2
        exit 1
    fi
    mv "$staging" "$path"
}

case "${1:-}" in
file) fetch_file "$2" "$3" "$4" ;;
*)
    echo "usage: deps.sh file <path> <url> <sha256>" >&2
    exit 2
    ;;
esac
