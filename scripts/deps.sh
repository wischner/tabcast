#!/usr/bin/env bash
#
# Fetch a third-party library into deps/, if it is not there already.
#
# Every library is pinned to an exact revision, and the pins live in the
# top-level Makefile. A library already present is left alone, so this
# costs nothing on every build after the first. The one exception is a
# pin that has moved: a managed inkerface checkout is moved to the new
# pin, unless it has local changes, which are never touched.
#
#   deps.sh git  <dir> <url> <revision>     a git repository
#   deps.sh file <path> <url> <sha256>      one file, checksummed
#
# Downloads go to a temporary name beside the destination and are
# renamed into place only once complete, so an interrupted download
# never leaves a half library that looks whole.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

set -euo pipefail

fetch_git() {
    local dir="$1" url="$2" revision="$3"

    if [[ -e "$dir" ]]; then
        if [[ ! -d "$dir/.git" ]]; then
            return 0
        fi
        local head
        head="$(git -C "$dir" rev-parse HEAD)"
        if [[ "$head" == "$revision" ]]; then
            return 0
        fi
        if [[ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ]]; then
            echo "deps       $dir has local changes, left at ${head:0:12}" \
                 "(the pin is ${revision:0:12})" >&2
            return 0
        fi
        echo "deps       moving $(basename "$dir") to ${revision:0:12}"
        git -C "$dir" fetch --quiet --depth 1 origin "$revision"
        git -C "$dir" checkout --quiet --detach "$revision"
        return 0
    fi

    echo "deps       downloading $(basename "$dir") ${revision:0:12} from $url"
    mkdir -p "$(dirname "$dir")"
    local staging="$dir.download"
    rm -rf "$staging"
    git init --quiet "$staging"
    git -C "$staging" remote add origin "$url"
    git -C "$staging" fetch --quiet --depth 1 origin "$revision"
    git -C "$staging" checkout --quiet --detach "$revision"
    mv "$staging" "$dir"
}

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
git)  fetch_git "$2" "$3" "$4" ;;
file) fetch_file "$2" "$3" "$4" ;;
*)
    echo "usage: deps.sh git <dir> <url> <revision>" >&2
    echo "       deps.sh file <path> <url> <sha256>" >&2
    exit 2
    ;;
esac
