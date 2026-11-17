# SPDX-License-Identifier: GPL-3.0-or-later
# shellcheck shell=bash
# Sourced by the dev scripts. Expects tools/dev/local.env (see local.env.example).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck disable=SC2034  # used by the scripts that source this file
ROOT=$(cd "$HERE/../.." && pwd)
# shellcheck disable=SC1091
source "$HERE/local.env"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=15 -o LogLevel=ERROR)
# The command is meant to be expanded here, then run on the PC.
# shellcheck disable=SC2029
pc() { ssh "${SSH_OPTS[@]}" "$PC_HOST" "$@" | tr -d '\r'; }
pc_put() { scp "${SSH_OPTS[@]}" -q "$1" "$PC_HOST:$2"; }
pc_get() { scp "${SSH_OPTS[@]}" -q "$PC_HOST:$1" "$2"; }
