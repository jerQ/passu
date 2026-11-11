#!/usr/bin/env bash
# Build the release firmware images: one merged .bin per chip, flashed at
# address 0x0, plus SHA256SUMS. Default settings only (8 groups,
# Enter over serial on).
#
#   . ~/esp/esp-idf/export.sh        # or the eim activate script
#   tools/release.sh v0.1.0
#
# Output: dist/passu-<version>-<target>.bin and dist/SHA256SUMS
set -euo pipefail

TARGETS=(esp32 esp32c3)

version="${1:-}"
if [[ ! "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "usage: $0 vMAJOR.MINOR.PATCH" >&2
    exit 2
fi
command -v idf.py >/dev/null || {
    echo "idf.py not found: load the ESP-IDF environment first" >&2
    exit 1
}

cd "$(dirname "$0")/.."

# A release must match a commit exactly.
if [[ -n "$(git status --porcelain)" ]]; then
    echo "working tree is not clean; commit or stash first" >&2
    exit 1
fi

rm -rf dist
mkdir dist

for target in "${TARGETS[@]}"; do
    dir="build-release-$target"
    echo "=== $target"
    rm -rf "$dir"
    # A fresh sdkconfig from sdkconfig.defaults (+ sdkconfig.defaults.<target>)
    idf.py -B "$dir" -D SDKCONFIG="$dir/sdkconfig" set-target "$target"
    idf.py -B "$dir" -D SDKCONFIG="$dir/sdkconfig" build merge-bin
    cp "$dir/merged-binary.bin" "dist/passu-$version-$target.bin"
done

(cd dist && sha256sum ./*.bin | sed 's| \./| |' > SHA256SUMS)
echo
cat dist/SHA256SUMS
