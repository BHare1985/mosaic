#!/usr/bin/env bash
# Download (shallow) the reference repos next to this script. Idempotent.
# Contents are git-ignored; run this after a fresh clone.
set -euo pipefail
cd "$(dirname "$0")"
fetch() { # name url
  if [ -d "$1/.git" ]; then git -C "$1" pull -q --ff-only || true
  else git clone -q --depth 1 "$2" "$1"; fi
  printf '%-10s %s\n' "$1" "$(git -C "$1" log -1 --format='%h %ad' --date=short)"
}
fetch zguide   https://github.com/booksbyus/zguide
fetch zyre     https://github.com/zeromq/zyre
