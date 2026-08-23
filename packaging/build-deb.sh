#!/usr/bin/env bash
#
# Assemble the three Tabcast parts into one Debian binary package.
# The builds themselves belong to the top-level Makefile; this script
# only validates and stages their finished outputs.
#
# MIT License (see: LICENSE)
# Copyright (C) 2026 Tomaz Stih
#

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
project_root="$(CDPATH= cd -- "$script_dir/.." && pwd)"
package_version="${1:-}"

if [[ -z "$package_version" ]]; then
    package_version="$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
        "$project_root/evangelist/src/manifest.json")"
fi

if ! dpkg --validate-version "$package_version" >/dev/null 2>&1; then
    echo "error: '$package_version' is not a valid Debian version" >&2
    exit 1
fi

package_arch="${PACKAGE_ARCH:-$(dpkg --print-architecture)}"
extension_version="$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
    "$project_root/evangelist/src/manifest.json")"

apostle="$project_root/bin/apostle"
disciple="$project_root/bin/disciple"
firefox="$project_root/bin/evangelist-$extension_version-firefox.xpi"
chrome="$project_root/bin/evangelist-$extension_version-chrome.zip"
control_template="$script_dir/debian/control.in"

for required in "$apostle" "$disciple" "$firefox" "$chrome" \
        "$control_template" "$project_root/LICENSE"; do
    if [[ ! -f "$required" ]]; then
        echo "error: required package input is missing: $required" >&2
        exit 1
    fi
done

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/tabcast-deb.XXXXXX")"
cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT

render_control() {
    local dependencies="$1"

    awk -v version="$package_version" \
        -v architecture="$package_arch" \
        -v dependencies="$dependencies" '
        {
            gsub(/@VERSION@/, version)
            gsub(/@ARCHITECTURE@/, architecture)
            gsub(/@DEPENDS@/, dependencies)
            print
        }
    ' "$control_template"
}

# Stage the payload under the conventional debian/<package> path. This
# also gives dpkg-shlibdeps the package context it expects while it
# calculates GTK and libc dependency names from the actual binaries.
scan_dir="$work_dir/scan"
package_root="$scan_dir/debian/tabcast"
mkdir -p "$package_root/DEBIAN"
install -Dm755 "$apostle" "$package_root/usr/bin/apostle"
install -Dm755 "$disciple" "$package_root/usr/bin/disciple"
install -Dm644 "$firefox" \
    "$package_root/usr/share/tabcast/evangelist/evangelist-firefox.xpi"
install -Dm644 "$chrome" \
    "$package_root/usr/share/tabcast/evangelist/evangelist-chrome.zip"
install -Dm644 "$project_root/LICENSE" \
    "$package_root/usr/share/doc/tabcast/copyright"

{
    printf '%s\n' \
        'Source: tabcast' \
        'Section: net' \
        'Priority: optional' \
        'Maintainer: Tomaz Stih <tomaz.stih@wischner.co.uk>' \
        ''
    render_control "libc6"
} >"$scan_dir/debian/control"
dependency_output="$(
    cd "$scan_dir"
    dpkg-shlibdeps -O \
        "debian/tabcast/usr/bin/apostle" \
        "debian/tabcast/usr/bin/disciple"
)"
dependencies="${dependency_output#shlibs:Depends=}"
if [[ -z "$dependencies" || "$dependencies" == "$dependency_output" ]]; then
    echo "error: dpkg-shlibdeps did not produce package dependencies" >&2
    exit 1
fi

render_control "$dependencies" >"$package_root/DEBIAN/control"

output_dir="$project_root/dist"
mkdir -p "$output_dir"
output_path="$output_dir/tabcast_${package_version}_${package_arch}.deb"
dpkg-deb --root-owner-group --build "$package_root" "$output_path"
echo "packaged $output_path"
