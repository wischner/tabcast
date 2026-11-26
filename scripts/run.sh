#!/usr/bin/env bash
#
# Run all of Tabcast on this machine, for `make run`.
#
#   Apostle      the daemon, logging here
#   Evangelist   in a Firefox of its own, with a throwaway profile
#   Disciple     on the emulator, in inkerface's preview window. On any
#                other platform, the reader itself, deployed beforehand,
#                which finds this Apostle by itself
#
# Closing the reader's window, or pressing P in it, its power button,
# stops everything. So does Ctrl+C.
#
#   run.sh <platform>
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

set -uo pipefail

platform="${1:-emulator}"
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

children=()
stop() {
    trap - EXIT INT TERM
    for pid in "${children[@]}"; do
        kill -TERM "$pid" 2>/dev/null
    done
    wait 2>/dev/null
}
trap stop EXIT
trap 'exit 130' INT TERM

"$root/bin/apostle" --foreground &
apostle=$!
children+=("$apostle")
sleep 0.5
if ! kill -0 "$apostle" 2>/dev/null; then
    echo "apostle    did not start, probably because one is already" \
         "running; using that one"
fi

python3 "$root/scripts/firefox.py" \
    "$root/build/evangelist/firefox" "$root/build/firefox" &
children+=("$!")

if [[ "$platform" == "emulator" ]]; then
    # The emulated reader's internal flash, so its Wi-Fi settings and its
    # name survive from one run to the next. Kept until make clean.
    storage="$root/build/disciple/storage"
    mkdir -p "$storage"
    INKERFACE_PREVIEW=1 INKERFACE_STORAGE_ROOT="$storage" "$root/bin/disciple"
else
    echo "disciple   is the $platform: switch it on and it finds this" \
         "Apostle. Ctrl+C stops."
    wait "$apostle"
fi
