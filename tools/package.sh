#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Assembles what a player extracts into the game folder, next to POP3.EXE, and zips it:
#   dinput8.dll   poppad.ini   poppad/glyphs/*.tga   LICENSE.txt
# Run after building (build/dinput8.dll) and after tools/build_glyphs.py (dist/poppad/glyphs).
# Usage: tools/package.sh [version]    the zip lands in dist/pop-pad-<version>.zip
set -euo pipefail
cd "$(dirname "$0")/.."

version=${1:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}
stage=dist/stage
zip=dist/pop-pad-$version.zip

for needed in build/dinput8.dll dist/poppad/glyphs/xbox.tga; do
  [ -f "$needed" ] || { echo "missing $needed: build the DLL and run tools/build_glyphs.py first" >&2; exit 1; }
done

rm -rf "$stage" "$zip"
mkdir -p "$stage/poppad/glyphs"
cp build/dinput8.dll "$stage/"
sed 's/$/\r/' poppad.ini > "$stage/poppad.ini"
cp dist/poppad/glyphs/*.tga "$stage/poppad/glyphs/"
cp assets/glyphs/SOURCE.md "$stage/poppad/glyphs/SOURCE.md"
cp LICENSE "$stage/LICENSE.txt"
(cd "$stage" && python3 -m zipfile -c "../pop-pad-$version.zip" dinput8.dll poppad.ini poppad LICENSE.txt)
rm -rf "$stage"
echo "$zip"
