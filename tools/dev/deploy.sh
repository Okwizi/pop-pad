#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build, then copy the DLL and an ini into the game folder on the test PC.
# Usage: deploy.sh user    the shipped poppad.ini, as a player would have it
#        deploy.sh dev     plus the command channel and screenshots (real pad)
#        deploy.sh devpad  plus the scripted virtual pad
# PROBE=1 also logs every change in what XInputGetState returns (see src/xi_hook.cpp).
# shellcheck source=tools/dev/common.sh
source "$(dirname "$0")/common.sh"
MODE=${1:-user}

cmake --build "$ROOT/build" >/dev/null

pc "if not exist $PC_LINK mklink /J $PC_LINK \"$PC_GAME_DIR\" >nul"
ini=$(mktemp)
sed 's/$/\r/' "$ROOT/poppad.ini" > "$ini"
# GLYPHS=1 switches the icon feature on for this deployment, whatever the shipped ini says.
[ -z "${GLYPHS:-}" ] || sed -i "s/^Enabled=.*/Enabled=$GLYPHS\r/" "$ini"
# STYLE=playstation (or xbox, switch, generic) forces an icon style for this deployment.
[ -z "${STYLE:-}" ] || sed -i "s/^Style=.*/Style=$STYLE\r/" "$ini"
case $MODE in
  user) ;;
  dev) printf '\r\n[Dev]\r\nVirtualPad=0\r\nCommands=1\r\nProbe=%s\r\n' "${PROBE:-0}" >> "$ini" ;;
  devpad) printf '\r\n[Dev]\r\nVirtualPad=1\r\nCommands=1\r\nProbe=%s\r\nIconTest=%s\r\n' "${PROBE:-0}" "${ICON_TEST:-0}" >> "$ini" ;;
  *) echo "unknown mode $MODE" >&2; exit 2 ;;
esac
pc "if not exist $PC_LINK\\poppad\\glyphs mkdir $PC_LINK\\poppad\\glyphs" >/dev/null
for atlas in "$ROOT"/dist/poppad/glyphs/*.tga; do
  name=$(basename "$atlas")
  remote=$(pc "for %F in ($PC_LINK\\poppad\\glyphs\\$name) do @echo %~zF" 2>/dev/null | tr -dc '0-9')
  [ "$remote" = "$(stat -c %s "$atlas")" ] || pc_put "$atlas" "$PC_LINK/poppad/glyphs/$name"
done
pc_put "$ROOT/build/dinput8.dll" "$PC_LINK/dinput8.dll"
pc_put "$ini" "$PC_LINK/poppad.ini"
rm -f "$ini"
pc "dir $PC_LINK\\dinput8.dll $PC_LINK\\poppad.ini | findstr /i \"dinput8 poppad\""
