#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Start the game on the test PC. A GUI program can only be started in the logged-in desktop
# session, which an SSH shell is not part of, so this goes through a one-off scheduled task.
# shellcheck source=tools/dev/common.sh
source "$(dirname "$0")/common.sh"
D="$(pc 'echo %USERPROFILE%')\\poppad-dev"

pc "if not exist poppad-dev mkdir poppad-dev" >/dev/null
pc "del /q poppad-dev\\launch.out 2>nul" >/dev/null || true
pc "del /q $PC_LINK\\poppad.log $PC_LINK\\poppad.done $PC_LINK\\poppad.cmd 2>nul" >/dev/null || true
# A marker left by an unclean exit makes the game stop on a "safe mode?" dialog at start.
case "$(pc 'tasklist /fi "imagename eq POP3.EXE" /fo csv /nh' || true)" in
  *POP3*) ;;
  *) pc "del /q $PC_LINK\\PrinceSM 2>nul" >/dev/null || true ;;
esac
pc_put "$HERE/launch.ps1" "poppad-dev/launch.ps1"
wrapper=$(mktemp)
printf '& "%s\\launch.ps1" -AppId %s *> "%s\\launch.out"\r\n' "$D" "$PC_STEAM_APPID" "$D" > "$wrapper"
pc_put "$wrapper" "poppad-dev/launch_task.ps1"
rm -f "$wrapper"
pc "schtasks /create /tn PopPadLaunch /sc once /st 00:00 /f /it /tr \"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File $D\\launch_task.ps1\" >nul 2>&1 & schtasks /run /tn PopPadLaunch >nul 2>&1" || true

for _ in $(seq 1 45); do
  sleep 2
  out=$(pc "type poppad-dev\\launch.out 2>nul" || true)
  case "$out" in *"game started"*|*"already running"*|*"did not start"*) break;; esac
done
echo "${out:-no output from launcher task}"
pc "schtasks /delete /tn PopPadLaunch /f >nul 2>&1" || true
