#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Send a command script (stdin, or arguments joined by ';') to the running game, wait for it to
# finish, and fetch any screenshots it took into shots/ as JPEG.
# Example: send.sh 'tap A; sleep 500; shot menu'
# shellcheck source=tools/dev/common.sh
source "$(dirname "$0")/common.sh"
TIMEOUT=${SEND_TIMEOUT:-60}

script=$(mktemp)
if [ $# -gt 0 ]; then printf '%s\n' "$*" | tr ';' '\n' | sed 's/^ *//' > "$script"; else cat > "$script"; fi
printf 'end\n' >> "$script"

pc "del /q $PC_LINK\\poppad.done 2>nul" >/dev/null || true
pc_put "$script" "$PC_LINK/poppad.cmd"

report=""
for _ in $(seq 1 "$TIMEOUT"); do
  report=$(pc "type $PC_LINK\\poppad.done 2>nul" || true)
  case "$report" in *done*) break;; esac
  sleep 1
done
echo "${report:-no reply from the game}"

mkdir -p "$ROOT/shots"
awk '$1=="shot"{print $2}' "$script" | while read -r name; do
  pc_get "$PC_LINK/poppad_shots/$name.bmp" "$ROOT/shots/$name.bmp" 2>/dev/null || { echo "missing shot $name"; continue; }
  python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2], quality=88)" "$ROOT/shots/$name.bmp" "$ROOT/shots/$name.jpg"
  rm -f "$ROOT/shots/$name.bmp"
done
rm -f "$script"
